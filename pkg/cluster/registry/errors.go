package registry

import "errors"

var (
	// ErrNodeNotFound is returned when a node is not found.
	ErrNodeNotFound = errors.New("node not found")

	// ErrNodeAlreadyExists is returned when trying to register a node that already exists.
	ErrNodeAlreadyExists = errors.New("node already exists")
)
