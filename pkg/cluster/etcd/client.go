// Package etcd provides a wrapper around etcd client with connection pooling
// and common operations for the hypervisor cluster.
package etcd

import (
	"context"
	"fmt"
	"sync"
	"time"

	clientv3 "go.etcd.io/etcd/client/v3"
	"go.uber.org/zap"
)

// Config holds the etcd client configuration.
type Config struct {
	Endpoints   []string      `mapstructure:"endpoints"`
	DialTimeout time.Duration `mapstructure:"dial_timeout"`
	Username    string        `mapstructure:"username"`
	Password    string        `mapstructure:"password"`

	// TLS configuration
	CertFile   string `mapstructure:"cert_file"`
	KeyFile    string `mapstructure:"key_file"`
	CAFile     string `mapstructure:"ca_file"`
	SkipVerify bool   `mapstructure:"skip_verify"`
}

// DefaultConfig returns the default etcd configuration.
func DefaultConfig() Config {
	return Config{
		Endpoints:   []string{"localhost:2379"},
		DialTimeout: 5 * time.Second,
	}
}

// Client wraps the etcd client with additional functionality.
type Client struct {
	client *clientv3.Client
	config Config
	logger *zap.Logger

	mu     sync.RWMutex
	closed bool
}

// New creates a new etcd client wrapper.
func New(cfg Config, logger *zap.Logger) (*Client, error) {
	if logger == nil {
		logger = zap.NewNop()
	}

	clientConfig := clientv3.Config{
		Endpoints:   cfg.Endpoints,
		DialTimeout: cfg.DialTimeout,
		Username:    cfg.Username,
		Password:    cfg.Password,
	}

	// TODO: Add TLS configuration if provided

	cli, err := clientv3.New(clientConfig)
	if err != nil {
		return nil, fmt.Errorf("failed to create etcd client: %w", err)
	}

	c := &Client{
		client: cli,
		config: cfg,
		logger: logger,
	}

	// Verify connection
	ctx, cancel := context.WithTimeout(context.Background(), cfg.DialTimeout)
	defer cancel()

	if _, err := cli.Status(ctx, cfg.Endpoints[0]); err != nil {
		cli.Close()
		return nil, fmt.Errorf("failed to connect to etcd: %w", err)
	}

	logger.Info("connected to etcd", zap.Strings("endpoints", cfg.Endpoints))
	return c, nil
}

// Close closes the etcd client connection.
func (c *Client) Close() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.closed {
		return nil
	}

	c.closed = true
	return c.client.Close()
}

// Raw returns the underlying etcd client for advanced operations.
func (c *Client) Raw() *clientv3.Client {
	return c.client
}

// Put stores a key-value pair in etcd.
func (c *Client) Put(ctx context.Context, key, value string, opts ...clientv3.OpOption) error {
	_, err := c.client.Put(ctx, key, value, opts...)
	if err != nil {
		return fmt.Errorf("etcd put failed: %w", err)
	}
	return nil
}

// Get retrieves a value by key from etcd.
func (c *Client) Get(ctx context.Context, key string, opts ...clientv3.OpOption) (string, error) {
	resp, err := c.client.Get(ctx, key, opts...)
	if err != nil {
		return "", fmt.Errorf("etcd get failed: %w", err)
	}

	if len(resp.Kvs) == 0 {
		return "", ErrKeyNotFound
	}

	return string(resp.Kvs[0].Value), nil
}

// GetWithPrefix retrieves all key-value pairs with a given prefix.
func (c *Client) GetWithPrefix(ctx context.Context, prefix string) (map[string]string, error) {
	resp, err := c.client.Get(ctx, prefix, clientv3.WithPrefix())
	if err != nil {
		return nil, fmt.Errorf("etcd get with prefix failed: %w", err)
	}

	result := make(map[string]string, len(resp.Kvs))
	for _, kv := range resp.Kvs {
		result[string(kv.Key)] = string(kv.Value)
	}

	return result, nil
}

// Delete removes a key from etcd.
func (c *Client) Delete(ctx context.Context, key string, opts ...clientv3.OpOption) error {
	_, err := c.client.Delete(ctx, key, opts...)
	if err != nil {
		return fmt.Errorf("etcd delete failed: %w", err)
	}
	return nil
}

// DeleteWithPrefix removes all keys with a given prefix.
func (c *Client) DeleteWithPrefix(ctx context.Context, prefix string) error {
	_, err := c.client.Delete(ctx, prefix, clientv3.WithPrefix())
	if err != nil {
		return fmt.Errorf("etcd delete with prefix failed: %w", err)
	}
	return nil
}

// Watch watches for changes on a key.
func (c *Client) Watch(ctx context.Context, key string, opts ...clientv3.OpOption) clientv3.WatchChan {
	return c.client.Watch(ctx, key, opts...)
}

// WatchWithPrefix watches for changes on all keys with a given prefix.
func (c *Client) WatchWithPrefix(ctx context.Context, prefix string) clientv3.WatchChan {
	return c.client.Watch(ctx, prefix, clientv3.WithPrefix())
}

// Grant creates a new lease with the given TTL in seconds.
func (c *Client) Grant(ctx context.Context, ttl int64) (*clientv3.LeaseGrantResponse, error) {
	return c.client.Grant(ctx, ttl)
}

// KeepAlive keeps a lease alive.
func (c *Client) KeepAlive(ctx context.Context, id clientv3.LeaseID) (<-chan *clientv3.LeaseKeepAliveResponse, error) {
	return c.client.KeepAlive(ctx, id)
}

// KeepAliveOnce sends a single keep-alive request.
func (c *Client) KeepAliveOnce(ctx context.Context, id clientv3.LeaseID) (*clientv3.LeaseKeepAliveResponse, error) {
	return c.client.KeepAliveOnce(ctx, id)
}

// Revoke revokes a lease.
func (c *Client) Revoke(ctx context.Context, id clientv3.LeaseID) error {
	_, err := c.client.Revoke(ctx, id)
	return err
}

// PutWithLease stores a key-value pair with an associated lease.
func (c *Client) PutWithLease(ctx context.Context, key, value string, leaseID clientv3.LeaseID) error {
	_, err := c.client.Put(ctx, key, value, clientv3.WithLease(leaseID))
	if err != nil {
		return fmt.Errorf("etcd put with lease failed: %w", err)
	}
	return nil
}

// Campaign starts a leader election campaign.
func (c *Client) Campaign(ctx context.Context, prefix, value string, leaseID clientv3.LeaseID) error {
	// Use etcd concurrency package for leader election
	// This is a simplified version; production use should use concurrency.Election
	key := prefix + "/" + fmt.Sprintf("%x", leaseID)

	// Try to create the key with the lease
	txn := c.client.Txn(ctx)
	txn = txn.If(clientv3.Compare(clientv3.CreateRevision(key), "=", 0))
	txn = txn.Then(clientv3.OpPut(key, value, clientv3.WithLease(leaseID)))

	resp, err := txn.Commit()
	if err != nil {
		return fmt.Errorf("campaign failed: %w", err)
	}

	if !resp.Succeeded {
		return ErrNotLeader
	}

	return nil
}

// WatchEvent represents a watch event from etcd.
type WatchEvent struct {
	Type  EventType
	Key   string
	Value string
}

// EventType represents the type of watch event.
type EventType int

const (
	EventTypePut EventType = iota
	EventTypeDelete
)

// KeyValue represents a key-value pair from etcd.
type KeyValue struct {
	Key   string
	Value string
}

// GetWithPrefixKV retrieves all key-value pairs with a given prefix as KeyValue slice.
func (c *Client) GetWithPrefixKV(ctx context.Context, prefix string) ([]KeyValue, error) {
	resp, err := c.client.Get(ctx, prefix, clientv3.WithPrefix())
	if err != nil {
		return nil, fmt.Errorf("etcd get with prefix failed: %w", err)
	}

	result := make([]KeyValue, 0, len(resp.Kvs))
	for _, kv := range resp.Kvs {
		result = append(result, KeyValue{
			Key:   string(kv.Key),
			Value: string(kv.Value),
		})
	}

	return result, nil
}

// PutWithTTL stores a key-value pair with a TTL.
func (c *Client) PutWithTTL(ctx context.Context, key, value string, ttlSeconds int64) error {
	lease, err := c.client.Grant(ctx, ttlSeconds)
	if err != nil {
		return fmt.Errorf("failed to create lease: %w", err)
	}

	_, err = c.client.Put(ctx, key, value, clientv3.WithLease(lease.ID))
	if err != nil {
		return fmt.Errorf("etcd put with ttl failed: %w", err)
	}
	return nil
}

// CreateIfNotExists creates a key only if it doesn't exist.
func (c *Client) CreateIfNotExists(ctx context.Context, key, value string) (bool, error) {
	txn := c.client.Txn(ctx)
	txn = txn.If(clientv3.Compare(clientv3.CreateRevision(key), "=", 0))
	txn = txn.Then(clientv3.OpPut(key, value))

	resp, err := txn.Commit()
	if err != nil {
		return false, fmt.Errorf("create if not exists failed: %w", err)
	}

	return resp.Succeeded, nil
}

// WatchPrefixEvents watches for changes on all keys with a given prefix and returns a channel of WatchEvents.
func (c *Client) WatchPrefixEvents(ctx context.Context, prefix string) <-chan WatchEvent {
	eventCh := make(chan WatchEvent, 100)

	go func() {
		defer close(eventCh)

		watchCh := c.client.Watch(ctx, prefix, clientv3.WithPrefix())
		for {
			select {
			case <-ctx.Done():
				return
			case resp, ok := <-watchCh:
				if !ok {
					return
				}
				for _, ev := range resp.Events {
					event := WatchEvent{
						Key:   string(ev.Kv.Key),
						Value: string(ev.Kv.Value),
					}
					if ev.Type == clientv3.EventTypePut {
						event.Type = EventTypePut
					} else {
						event.Type = EventTypeDelete
					}
					select {
					case eventCh <- event:
					case <-ctx.Done():
						return
					}
				}
			}
		}
	}()

	return eventCh
}
