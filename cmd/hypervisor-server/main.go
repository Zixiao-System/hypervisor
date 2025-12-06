package main

import (
	"context"
	"fmt"
	"os"
	"os/signal"
	"syscall"

	"hypervisor/internal/server"

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
		Use:   "hypervisor-server",
		Short: "Hypervisor control plane server",
		Long: `Hypervisor Server is the control plane component of the distributed
cloud operating system. It manages cluster coordination, node registration,
and instance scheduling.`,
		RunE: runServer,
	}

	// Flags
	rootCmd.PersistentFlags().StringVar(&cfgFile, "config", "", "config file (default: /etc/hypervisor/server.yaml)")
	rootCmd.PersistentFlags().StringVar(&logLevel, "log-level", "info", "log level (debug, info, warn, error)")

	// Version command
	rootCmd.AddCommand(&cobra.Command{
		Use:   "version",
		Short: "Print version information",
		Run: func(cmd *cobra.Command, args []string) {
			fmt.Printf("hypervisor-server %s\n", Version)
			fmt.Printf("  Build Time: %s\n", BuildTime)
			fmt.Printf("  Git Commit: %s\n", GitCommit)
		},
	})

	if err := rootCmd.Execute(); err != nil {
		os.Exit(1)
	}
}

func runServer(cmd *cobra.Command, args []string) error {
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

	logger.Info("starting hypervisor server",
		zap.String("version", Version),
		zap.String("grpc_addr", config.GRPCAddr),
	)

	// Create server
	srv, err := server.New(config, logger)
	if err != nil {
		return fmt.Errorf("failed to create server: %w", err)
	}

	// Start server
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	if err := srv.Start(ctx); err != nil {
		return fmt.Errorf("failed to start server: %w", err)
	}

	// Wait for shutdown signal
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	<-sigCh
	logger.Info("received shutdown signal")

	// Graceful shutdown
	if err := srv.Stop(); err != nil {
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

func loadConfig(cfgFile string) (server.Config, error) {
	config := server.DefaultConfig()

	if cfgFile != "" {
		viper.SetConfigFile(cfgFile)
	} else {
		viper.SetConfigName("server")
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
