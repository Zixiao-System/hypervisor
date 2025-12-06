package driver

import "errors"

var (
	// ErrInstanceNotFound is returned when an instance is not found.
	ErrInstanceNotFound = errors.New("instance not found")

	// ErrInstanceAlreadyExists is returned when trying to create an instance that already exists.
	ErrInstanceAlreadyExists = errors.New("instance already exists")

	// ErrInstanceRunning is returned when an operation requires a stopped instance.
	ErrInstanceRunning = errors.New("instance is running")

	// ErrInstanceStopped is returned when an operation requires a running instance.
	ErrInstanceStopped = errors.New("instance is stopped")

	// ErrNotConnected is returned when the driver is not connected.
	ErrNotConnected = errors.New("driver not connected")

	// ErrOperationFailed is returned when an operation fails.
	ErrOperationFailed = errors.New("operation failed")

	// ErrNotSupported is returned when an operation is not supported.
	ErrNotSupported = errors.New("operation not supported")

	// ErrInvalidSpec is returned when the instance spec is invalid.
	ErrInvalidSpec = errors.New("invalid instance specification")
)
