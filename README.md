`$JDTAUS: README.md 9649 2026-07-30 09:32:08Z schulte $`

# Abagnale - Algorithmic Trading System

> **A high-performance algorithmic trading platform built in ISO C for traders and engineering teams that value deterministic execution, transparency, and operational control.**

Abagnale is an open-source algorithmic trading engine designed for developers, quantitative traders, and organizations building automated trading infrastructure. Rather than hiding execution behind opaque frameworks, Abagnale provides a modular, native implementation focused on performance, reliability, and extensibility.

Built as a standalone application with a companion administration utility, Abagnale combines exchange connectivity, configurable trading algorithms, PostgreSQL-backed persistence, and multithreaded execution into a cohesive platform suitable for research, deployment, and long-running production workloads.

---

# Why Abagnale?

Financial markets reward speed, consistency, and disciplined execution. Manual trading introduces latency, emotion, and operational risk. Abagnale enables systematic trading by transforming repeatable investment logic into reliable, continuously operating software.

Whether you are an individual quantitative trader, a proprietary trading firm, or an engineering organization building automated investment infrastructure, Abagnale provides a foundation that emphasizes:

* Native performance
* Predictable execution
* Extensible architecture
* Open implementation
* Production-ready deployment
* Modern software engineering practices

---

# Executive Summary

Abagnale is not a scripting framework—it is a native trading application.

The project is implemented in modern ISO C, minimizing unnecessary abstraction while maximizing execution efficiency. The architecture separates exchange integrations, trading algorithms, configuration, persistence, networking, and utility libraries into independent modules that can evolve without affecting the entire platform.

The result is a platform suitable for:

* Quantitative traders
* Proprietary trading firms
* Algorithm developers
* Financial software companies
* Infrastructure engineers
* Researchers
* Advanced individual investors

---

# Business Value

## Reduce Operational Risk

Automated execution eliminates repetitive manual tasks while providing consistent behavior across market sessions.

## Increase Trading Discipline

Strategies execute according to predefined rules instead of human emotion.

## Build on Open Technology

Because the platform is open source, organizations maintain complete visibility into trading logic, infrastructure, and data flow.

## Lower Infrastructure Costs

A native implementation reduces runtime overhead compared to heavyweight managed environments.

## Enable Continuous Improvement

Modular components make it practical to introduce new exchanges, algorithms, and operational tooling without redesigning the entire system.

---

# Core Capabilities

Abagnale includes support for:

* Modular trading algorithms
* Exchange abstraction layer
* Coinbase integration
* Bitvavo integration
* PostgreSQL persistence
* HTTP services
* JSON processing
* Configuration-driven deployment
* Multithreaded execution
* Administrative command-line tooling
* Systemd service integration
* Trend-following algorithm implementation
* Runtime configuration parsing
* Monitoring and process utilities

---

# Architecture

Abagnale is organized as a collection of focused modules rather than a monolithic application.

```
                +----------------------+
                | Configuration Files  |
                +----------+-----------+
                           |
                           v
                  Configuration Engine
                           |
            +--------------+--------------+
            |                             |
            v                             v
     Trading Algorithms         Exchange Layer
            |                             |
            +--------------+--------------+
                           |
                           v
                   Trading Engine
                           |
             +-------------+-------------+
             |                           |
             v                           v
      PostgreSQL Database         HTTP Services
             |
             v
      Analytics & History
```

The architecture encourages clear separation between:

* Strategy logic
* Exchange communication
* Persistence
* Infrastructure
* Utility libraries

This modular design simplifies maintenance and future expansion.

---

# Technical Highlights

## Portable Native Implementation

Abagnale is written in ISO C, emphasizing standards compliance, portability, efficiency, and low runtime overhead. By avoiding unnecessary compiler-specific extensions where practical, the platform remains maintainable across diverse environments while delivering the predictable performance expected of native applications.

## Modular Exchange Layer

Exchange implementations are isolated behind a common interface. Current implementations include:

* Coinbase
* Bitvavo

Additional exchanges can be integrated without redesigning trading algorithms.

## Strategy Framework

Trading strategies are implemented independently from exchange connectivity.

The repository currently includes a trend-based algorithm while providing an extensible architecture for additional strategies.

## PostgreSQL Integration

Persistent storage enables:

* historical trade data
* market information
* analytics
* reporting
* operational state

## Multithreaded Design

The build configuration enables multithreaded execution, allowing concurrent processing of market data and trading activities.

## Configuration-Driven Operation

Behavior is controlled through configuration files rather than recompilation, enabling different deployments with minimal operational effort.

---

# Operational Features

Abagnale includes infrastructure expected from long-running production services:

* systemd service definition
* administrative utility (`abagnalectl`)
* configuration validation
* logging support
* HTTP interface
* structured JSON processing
* reusable utility libraries
* process management helpers

---

# Technology Stack

| Component         | Technology               |
| ----------------- | ------------------------ |
| Language          | ISO C                    |
| Build System      | CMake                    |
| Alternative Build | BSD Make                 |
| Database          | PostgreSQL               |
| Network Layer     | Mongoose                 |
| Configuration     | Custom parser            |
| Data Format       | JSON                     |
| Deployment        | Linux services (systemd) |
| Concurrency       | Multithreaded            |

---

# Why ISO C?

Abagnale is implemented in **portable ISO C**, reflecting a deliberate engineering philosophy rather than a language preference. By adhering closely to the ISO C standard, the platform remains lightweight, portable, and easy to build across a wide range of environments using conforming compilers.

This standards-based approach minimizes unnecessary dependencies, promotes long-term maintainability, and ensures the codebase remains accessible to developers familiar with one of the industry's most enduring programming languages.

---

# Intended Users

## Independent Traders

Automate proven trading strategies while retaining complete control over execution.

## Quantitative Researchers

Experiment with new algorithmic approaches on a modular platform.

## Proprietary Trading Firms

Deploy native infrastructure optimized for performance, transparency, and maintainability.

## Financial Software Teams

Extend the platform with additional exchanges, analytics, or proprietary strategies.

---

# Design Principles

Abagnale follows several consistent engineering principles:

* Native performance over unnecessary abstraction
* Modular architecture
* Clear separation of concerns
* Configuration instead of hardcoding
* Open interfaces
* Production-oriented deployment
* Maintainable codebase
* Portable build system

---

# Repository Structure

```
abagnale.c               Core trading application
abagnalectl.c            Administrative utility
algorithm-trend.c        Trend strategy
exchange.c               Exchange abstraction
exchange-coinbase.c      Coinbase implementation
exchange-bitvavo.c       Bitvavo implementation
database-postgresql.*    PostgreSQL integration
http.*                   HTTP services
json.*                   JSON processing
config.*                 Configuration parser
thread.*                 Threading support
time.*                   Time utilities
```

See `INTRO.txt` for further details.

---

# Building

Abagnale can be built using CMake or BSD make.

Typical dependencies include:

* C compiler with modern C support
* CMake
* PostgreSQL client libraries (including ECPG)
* Threads
* Standard system development libraries

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

On most systems the application can be built by just executing

```bash
make
```

---

# Deployment

The project includes:

* configuration files
* demonstration configuration
* systemd service definition
* administrative tooling

These components make it suitable for continuous execution on trading servers or dedicated infrastructure.

---

# Extending Abagnale

The architecture is designed for future growth.

Typical extension points include:

* additional exchanges
* new trading algorithms
* custom risk management
* portfolio management
* execution models
* analytics
* monitoring integrations
* reporting

---

# Vision

Abagnale aims to provide a transparent, high-performance foundation for algorithmic trading that organizations can understand, audit, extend, and operate with confidence.

Rather than being tied to a proprietary ecosystem, the platform embraces open engineering practices and modular design, allowing traders and developers to evolve their trading infrastructure alongside changing markets.

---

# License

This project is distributed under the ISC License. See `LICENSE.txt` for details.
