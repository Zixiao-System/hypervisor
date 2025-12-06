package main

import (
	"context"
	"fmt"
	"os"
	"os/signal"
	"syscall"

	"hypervisor/internal/agent"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"go.uber.org/zap"
	"go.uber.org/zap/zapcore"
)

var (
	Version   = "dev"
	BuildTime = "unknown"
	GitCommit = "unknown"
)

var (
	cfgFile  string
	logLevel string
)

func main() {
	rootCmd := &cobra.Command{
		Use:   "hypervisor-agent",
		Short: "Hypervisor compute node agent",
		Long: `Hypervisor Agent runs on compute nodes and manages local instances
(VMs, containers, and microVMs). It registers with the control plane,
sends heartbeats, and executes instance lifecycle operations.`,
		RunE: runAgent,
	}

	// Flags
	rootCmd.PersistentFlags().StringVar(&cfgFile, "config", "", "config file (default: /etc/hypervisor/agent.yaml)")
	rootCmd.PersistentFlags().StringVar(&logLevel, "log-level", "info", "log level (debug, info, warn, error)")

	// Node configuration flags
	rootCmd.Flags().String("node-id", "", "node ID (auto-generated if empty)")
	rootCmd.Flags().String("hostname", "", "hostname (auto-detected if empty)")
	rootCmd.Flags().String("ip", "", "node IP address")
	rootCmd.Flags().Int("port", 50052, "agent gRPC port")
	rootCmd.Flags().String("role", "worker", "node role (worker or master)")
	rootCmd.Flags().String("region", "default", "region name")
	rootCmd.Flags().String("zone", "default", "availability zone")
	rootCmd.Flags().String("server-addr", "localhost:50051", "server address")

	// Bind flags to viper
	viper.BindPFlag("node_id", rootCmd.Flags().Lookup("node-id"))
	viper.BindPFlag("hostname", rootCmd.Flags().Lookup("hostname"))
	viper.BindPFlag("ip", rootCmd.Flags().Lookup("ip"))
	viper.BindPFlag("port", rootCmd.Flags().Lookup("port"))
	viper.BindPFlag("role", rootCmd.Flags().Lookup("role"))
	viper.BindPFlag("region", rootCmd.Flags().Lookup("region"))
	viper.BindPFlag("zone", rootCmd.Flags().Lookup("zone"))
	viper.BindPFlag("server_addr", rootCmd.Flags().Lookup("server-addr"))

	// Version command
	rootCmd.AddCommand(&cobra.Command{
		Use:   "version",
		Short: "Print version information",
		Run: func(cmd *cobra.Command, args []string) {
			fmt.Printf("hypervisor-agent %s\n", Version)
			fmt.Printf("  Build Time: %s\n", BuildTime)
			fmt.Printf("  Git Commit: %s\n", GitCommit)
		},
	})

	if err := rootCmd.Execute(); err != nil {
		os.Exit(1)
	}
}

func runAgent(cmd *cobra.Command, args []string) error {
	// Initialize logger
	logger, err := initLogger(logLevel)
	if err != nil {
		return fmt.Errorf("failed to initialize logger: %w", err)
	}
	defer logger.Sync()

	// Load configuration
	config, err := loadConfig(cfgFile)
	if err != nil {
		return fmt.Errorf("failed to load config: %w", err)
	}

	logger.Info("starting hypervisor agent",
		zap.String("version", Version),
		zap.String("hostname", config.Hostname),
		zap.String("role", config.Role),
	)

	// Create agent
	ag, err := agent.New(config, logger)
	if err != nil {
		return fmt.Errorf("failed to create agent: %w", err)
	}

	// Start agent
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	if err := ag.Start(ctx); err != nil {
		return fmt.Errorf("failed to start agent: %w", err)
	}

	// Wait for shutdown signal
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	<-sigCh
	logger.Info("received shutdown signal")

	// Graceful shutdown
	if err := ag.Stop(); err != nil {
		logger.Error("error during shutdown", zap.Error(err))
	}

	return nil
}

func initLogger(level string) (*zap.Logger, error) {
	var zapLevel zapcore.Level
	if err := zapLevel.UnmarshalText([]byte(level)); err != nil {
		zapLevel = zapcore.InfoLevel
	}

	config := zap.Config{
		Level:            zap.NewAtomicLevelAt(zapLevel),
		Development:      false,
		Encoding:         "json",
		EncoderConfig:    zap.NewProductionEncoderConfig(),
		OutputPaths:      []string{"stdout"},
		ErrorOutputPaths: []string{"stderr"},
	}

	return config.Build()
}

func loadConfig(cfgFile string) (agent.Config, error) {
	config := agent.DefaultConfig()

	if cfgFile != "" {
		viper.SetConfigFile(cfgFile)
	} else {
		viper.SetConfigName("agent")
		viper.SetConfigType("yaml")
		viper.AddConfigPath("/etc/hypervisor")
		viper.AddConfigPath("$HOME/.hypervisor")
		viper.AddConfigPath("./configs")
		viper.AddConfigPath(".")
	}

	viper.SetEnvPrefix("HYPERVISOR")
	viper.AutomaticEnv()

	if err := viper.ReadInConfig(); err != nil {
		if _, ok := err.(viper.ConfigFileNotFoundError); !ok {
			return config, err
		}
		// Config file not found; use defaults
	}

	if err := viper.Unmarshal(&config); err != nil {
		return config, err
	}

	return config, nil
}
