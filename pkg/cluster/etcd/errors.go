package etcd

import "errors"

var (
	// ErrKeyNotFound is returned when a key is not found in etcd.
	ErrKeyNotFound = errors.New("key not found")

	// ErrNotLeader is returned when the node is not the leader.
	ErrNotLeader = errors.New("not the leader")

	// ErrLeaseExpired is returned when a lease has expired.
	ErrLeaseExpired = errors.New("lease expired")
)
