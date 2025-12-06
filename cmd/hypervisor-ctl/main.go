package main

import (
	"context"
	"fmt"
	"os"
	"text/tabwriter"
	"time"

	"github.com/spf13/cobra"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

var (
	Version   = "dev"
	BuildTime = "unknown"
	GitCommit = "unknown"
)

var (
	serverAddr string
	output     string
)

func main() {
	rootCmd := &cobra.Command{
		Use:   "hypervisor-ctl",
		Short: "Hypervisor command-line interface",
		Long: `hypervisor-ctl is the CLI tool for managing the hypervisor cluster.
It provides commands for managing nodes, instances, and cluster operations.`,
	}

	// Global flags
	rootCmd.PersistentFlags().StringVar(&serverAddr, "server", "localhost:50051", "server address")
	rootCmd.PersistentFlags().StringVarP(&output, "output", "o", "table", "output format (table, json, yaml)")

	// Add commands
	rootCmd.AddCommand(versionCmd())
	rootCmd.AddCommand(nodeCmd())
	rootCmd.AddCommand(instanceCmd())
	rootCmd.AddCommand(clusterCmd())

	if err := rootCmd.Execute(); err != nil {
		os.Exit(1)
	}
}

func versionCmd() *cobra.Command {
	return &cobra.Command{
		Use:   "version",
		Short: "Print version information",
		Run: func(cmd *cobra.Command, args []string) {
			fmt.Printf("hypervisor-ctl %s\n", Version)
			fmt.Printf("  Build Time: %s\n", BuildTime)
			fmt.Printf("  Git Commit: %s\n", GitCommit)
		},
	}
}

func nodeCmd() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "node",
		Short: "Manage cluster nodes",
	}

	// node list
	cmd.AddCommand(&cobra.Command{
		Use:   "list",
		Short: "List all nodes",
		RunE: func(cmd *cobra.Command, args []string) error {
			return listNodes()
		},
	})

	// node get <id>
	cmd.AddCommand(&cobra.Command{
		Use:   "get <node-id>",
		Short: "Get node details",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			return getNode(args[0])
		},
	})

	// node drain <id>
	cmd.AddCommand(&cobra.Command{
		Use:   "drain <node-id>",
		Short: "Drain a node (prepare for maintenance)",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			return drainNode(args[0])
		},
	})

	// node cordon <id>
	cmd.AddCommand(&cobra.Command{
		Use:   "cordon <node-id>",
		Short: "Mark node as unschedulable",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			return cordonNode(args[0])
		},
	})

	// node uncordon <id>
	cmd.AddCommand(&cobra.Command{
		Use:   "uncordon <node-id>",
		Short: "Mark node as schedulable",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			return uncordonNode(args[0])
		},
	})

	return cmd
}

func instanceCmd() *cobra.Command {
	cmd := &cobra.Command{
		Use:     "instance",
		Aliases: []string{"vm", "container"},
		Short:   "Manage instances",
	}

	// instance list
	listCmd := &cobra.Command{
		Use:   "list",
		Short: "List instances",
		RunE: func(cmd *cobra.Command, args []string) error {
			nodeID, _ := cmd.Flags().GetString("node")
			instanceType, _ := cmd.Flags().GetString("type")
			return listInstances(nodeID, instanceType)
		},
	}
	listCmd.Flags().StringP("node", "n", "", "filter by node ID")
	listCmd.Flags().StringP("type", "t", "", "filter by type (vm, container, microvm)")
	cmd.AddCommand(listCmd)

	// instance get <id>
	cmd.AddCommand(&cobra.Command{
		Use:   "get <instance-id>",
		Short: "Get instance details",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			return getInstance(args[0])
		},
	})

	// instance create
	createCmd := &cobra.Command{
		Use:   "create",
		Short: "Create a new instance",
		RunE: func(cmd *cobra.Command, args []string) error {
			name, _ := cmd.Flags().GetString("name")
			instanceType, _ := cmd.Flags().GetString("type")
			image, _ := cmd.Flags().GetString("image")
			cpus, _ := cmd.Flags().GetInt("cpus")
			memory, _ := cmd.Flags().GetInt("memory")
			return createInstance(name, instanceType, image, cpus, memory)
		},
	}
	createCmd.Flags().String("name", "", "instance name (required)")
	createCmd.Flags().StringP("type", "t", "vm", "instance type (vm, container, microvm)")
	createCmd.Flags().StringP("image", "i", "", "image name (required)")
	createCmd.Flags().Int("cpus", 1, "number of CPUs")
	createCmd.Flags().Int("memory", 512, "memory in MB")
	createCmd.MarkFlagRequired("name")
	createCmd.MarkFlagRequired("image")
	cmd.AddCommand(createCmd)

	// instance start <id>
	cmd.AddCommand(&cobra.Command{
		Use:   "start <instance-id>",
		Short: "Start an instance",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			return startInstance(args[0])
		},
	})

	// instance stop <id>
	stopCmd := &cobra.Command{
		Use:   "stop <instance-id>",
		Short: "Stop an instance",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			force, _ := cmd.Flags().GetBool("force")
			return stopInstance(args[0], force)
		},
	}
	stopCmd.Flags().BoolP("force", "f", false, "force stop")
	cmd.AddCommand(stopCmd)

	// instance delete <id>
	deleteCmd := &cobra.Command{
		Use:   "delete <instance-id>",
		Short: "Delete an instance",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			force, _ := cmd.Flags().GetBool("force")
			return deleteInstance(args[0], force)
		},
	}
	deleteCmd.Flags().BoolP("force", "f", false, "force delete")
	cmd.AddCommand(deleteCmd)

	return cmd
}

func clusterCmd() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "cluster",
		Short: "Cluster operations",
	}

	// cluster info
	cmd.AddCommand(&cobra.Command{
		Use:   "info",
		Short: "Show cluster information",
		RunE: func(cmd *cobra.Command, args []string) error {
			return clusterInfo()
		},
	})

	return cmd
}

// Helper functions for gRPC calls

func getClient() (*grpc.ClientConn, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	return grpc.DialContext(ctx, serverAddr, grpc.WithTransportCredentials(insecure.NewCredentials()))
}

func listNodes() error {
	fmt.Println("Fetching nodes from", serverAddr)

	// TODO: Implement actual gRPC call
	// For now, just show a placeholder
	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w, "NODE ID\tHOSTNAME\tSTATUS\tROLE\tREGION\tZONE\tCPU\tMEMORY")
	fmt.Fprintln(w, "node-1\tworker-1\tReady\tworker\tus-west\tzone-a\t4/8\t8Gi/16Gi")
	w.Flush()

	return nil
}

func getNode(id string) error {
	fmt.Printf("Getting node: %s\n", id)
	// TODO: Implement
	return nil
}

func drainNode(id string) error {
	fmt.Printf("Draining node: %s\n", id)
	// TODO: Implement
	return nil
}

func cordonNode(id string) error {
	fmt.Printf("Cordoning node: %s\n", id)
	// TODO: Implement
	return nil
}

func uncordonNode(id string) error {
	fmt.Printf("Uncordoning node: %s\n", id)
	// TODO: Implement
	return nil
}

func listInstances(nodeID, instanceType string) error {
	fmt.Println("Fetching instances from", serverAddr)

	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w, "INSTANCE ID\tNAME\tTYPE\tSTATUS\tNODE\tCPU\tMEMORY")
	fmt.Fprintln(w, "i-123456\tmy-vm\tvm\trunning\tnode-1\t2\t4Gi")
	w.Flush()

	return nil
}

func getInstance(id string) error {
	fmt.Printf("Getting instance: %s\n", id)
	// TODO: Implement
	return nil
}

func createInstance(name, instanceType, image string, cpus, memory int) error {
	fmt.Printf("Creating instance: %s (type=%s, image=%s, cpus=%d, memory=%dMB)\n",
		name, instanceType, image, cpus, memory)
	// TODO: Implement
	return nil
}

func startInstance(id string) error {
	fmt.Printf("Starting instance: %s\n", id)
	// TODO: Implement
	return nil
}

func stopInstance(id string, force bool) error {
	fmt.Printf("Stopping instance: %s (force=%v)\n", id, force)
	// TODO: Implement
	return nil
}

func deleteInstance(id string, force bool) error {
	fmt.Printf("Deleting instance: %s (force=%v)\n", id, force)
	// TODO: Implement
	return nil
}

func clusterInfo() error {
	fmt.Println("Cluster Information")
	fmt.Println("===================")
	fmt.Println("Cluster ID:    default")
	fmt.Println("Cluster Name:  hypervisor-cluster")
	fmt.Println("Version:       0.1.0")
	fmt.Println()
	fmt.Println("Nodes:         3 (3 ready)")
	fmt.Println("Instances:     12")
	fmt.Println()
	fmt.Println("Resources:")
	fmt.Println("  CPU:         24 cores (12 allocated)")
	fmt.Println("  Memory:      48 GB (24 GB allocated)")
	fmt.Println("  Disk:        1 TB (500 GB allocated)")

	return nil
}
