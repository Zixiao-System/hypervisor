package heartbeat

import "errors"

var (
	// ErrNoLease is returned when there is no lease for the node.
	ErrNoLease = errors.New("no lease found for node")

	// ErrAlreadyRunning is returned when the service is already running.
	ErrAlreadyRunning = errors.New("heartbeat service is already running")
)
