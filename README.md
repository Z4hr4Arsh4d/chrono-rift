# Chrono Rift
## Real-Time Multiplayer Tower Simulation System

Chrono Rift is a real-time multiplayer tower simulation system developed in C++ on Linux Ubuntu using advanced Operating System concepts such as multiprocessing, synchronization, inter-process communication (IPC), shared memory, and scheduling.

The project simulates a dynamic tower environment where multiple human-controlled processes and NPC systems interact concurrently inside a continuously updating world. The system focuses heavily on low-level systems programming while maintaining modular multiplayer gameplay architecture.

---

# Table of Contents

- Introduction
- Project Objectives
- Features
- Gameplay Overview
- Multiplayer Architecture
- Operating System Concepts Used
- System Architecture
- Project Structure
- Installation
- Compilation
- Running the Project
- Test Mode
- Core Components
- Process Communication
- Synchronization
- Scheduler System
- Memory Management
- Challenges Faced
- Learning Outcomes
- Future Improvements
- Team Members
- License

---

# Introduction

Chrono Rift is designed as a hybrid between a real-time simulation engine and a multiplayer survival system.

Players enter a simulated tower structure containing multiple floors, hazards, NPCs, and concurrent gameplay events. The entire simulation continuously updates through a centralized tick-based scheduler.

Unlike traditional game-engine-based projects, Chrono Rift directly utilizes Operating System resources and process-level execution to demonstrate practical concurrency and synchronization concepts.

Each multiplayer player executes as an independent operating system process, enabling realistic concurrent execution behavior.

---

# Project Objectives

The primary objectives of this project are:

- To implement a real-time multiplayer simulation system
- To apply practical Operating System concepts
- To demonstrate synchronization and IPC mechanisms
- To build modular and scalable software architecture
- To simulate concurrent player interactions
- To safely manage shared game state
- To explore scheduling and resource allocation techniques

---

# Features

## Real-Time Simulation

The world continuously updates using a synchronized global scheduler and tick system.

## Multiplayer Gameplay

Multiple human players execute through independent operating system processes.

## Dynamic Tower Environment

The tower contains multiple floors with changing hazards, resources, and gameplay events.

## NPC System

NPC entities independently interact with players and the environment.

## Resource Management

Players collect and manage resources while surviving tower events.

## Event Engine

The simulation supports randomized and scheduled gameplay events.

## Configurable Test Mode

Built-in debugging and testing functionality supports simulation analysis and validation.

---

# Gameplay Overview

Players navigate through tower floors while:

- collecting resources
- avoiding hazards
- interacting with NPCs
- surviving timed events
- competing with other players
- progressing through increasingly difficult stages

The game world evolves continuously as the scheduler updates all entities in real time.

---

# Multiplayer Architecture

The multiplayer architecture launches:

- One Arbiter Process
- Two Human Interface Processes (HIPs)
- One ASP Process
- Shared NPC management system

Each HIP executes as a completely separate operating system process rather than a thread.

This design ensures:

- realistic concurrency
- process isolation
- scalable multiplayer execution
- safe memory separation

---

# Operating System Concepts Used

The project heavily utilizes core Operating System principles.

## Process Creation

Multiple independent processes are created using Linux process handling mechanisms.

## Inter-Process Communication (IPC)

Processes communicate through shared memory and synchronization primitives.

## Synchronization

Semaphores and locking mechanisms prevent race conditions and inconsistent state updates.

## Scheduling

The simulation updates through a centralized tick scheduler.

## Shared Memory

Processes safely access and modify the shared simulation state.

## Concurrency

Multiple systems execute simultaneously in real time.

---

# System Architecture

```text
+------------------------------------------------+
|                 Player Processes               |
+------------------------------------------------+
|              Human Interface Layer             |
+------------------------------------------------+
|              Game Logic / Scheduler            |
+------------------------------------------------+
|         Shared Tower Simulation State          |
+------------------------------------------------+
|           IPC + Synchronization Layer          |
+------------------------------------------------+
|              Linux Operating System            |
+------------------------------------------------+
```

---

# Project Structure

```bash
chrono-rift/
│
├── arbiter/                # Arbiter process implementation
├── asp/                    # ASP process implementation
├── hip/                    # Human Interface Processes
├── common/                 # Shared utilities and structures
│
├── Dockerfile              # Docker container configuration
├── Makefile                # Build automation
├── run_mp.sh               # Multiplayer launcher script
├── requirements.txt        # Project dependencies
├── turnaround.csv          # Simulation / output data
├── Chrono_Rift_Report.pdf  # Project documentation
```

---

# Installation

## Requirements

- Linux Ubuntu
- g++
- Bash
- POSIX-compliant environment
- Make utility

Install required tools:

```bash
sudo apt update
sudo apt install g++ make
```

---

# Compilation

Compile the project using:

```bash
make
```

Or manually compile using g++ if required.

---

# Running the Project

## Multiplayer Mode

```bash
./run_mp.sh
```

---

# Test Mode

The project includes configurable testing and debugging support.

Example:

```bash
./chrono_rift --test
```

Test mode supports:

- scheduler validation
- synchronization testing
- stress testing
- debugging
- performance analysis

---

# Core Components

## Arbiter

Coordinates multiplayer synchronization and global simulation control.

## HIP (Human Interface Process)

Handles human-controlled player interaction and gameplay execution.

## ASP

Manages auxiliary simulation behavior and background processing.

## Common Module

Contains shared data structures, utilities, and synchronization resources.

---

# Process Communication

Processes communicate using IPC mechanisms including:

- shared memory
- synchronization flags
- semaphores
- controlled message exchange

This ensures synchronized world-state updates across all active processes.

---

# Synchronization

To prevent race conditions and inconsistent shared-memory access, synchronization mechanisms are implemented throughout the system.

Key synchronization goals include:

- protecting shared resources
- coordinating scheduler updates
- maintaining consistent tower state
- controlling multiplayer interactions

---

# Scheduler System

The simulation operates using a centralized tick scheduler.

Each tick performs:

- player updates
- NPC movement
- event processing
- environment updates
- synchronization checks

This ensures deterministic and stable simulation behavior.

---

# Memory Management

Dynamic memory allocation is used to manage:

- players
- NPC systems
- shared tower structures
- inventories
- event queues

The project emphasizes efficient resource handling and safe memory cleanup.

---

# Challenges Faced

Major development challenges included:

- synchronization between processes
- shared memory consistency
- multiplayer coordination
- race condition prevention
- scheduler stability
- scalable architecture design
- real-time event handling

---

# Learning Outcomes

This project provided practical experience in:

- Operating Systems
- Linux system programming
- Concurrency
- Process synchronization
- IPC mechanisms
- Multiplayer system architecture
- Real-time simulation systems
- Modular software engineering

---

# Future Improvements

Potential future enhancements include:

- graphical user interface
- networked multiplayer support
- advanced AI behavior
- procedural tower generation
- persistent save system
- improved gameplay mechanics
- audio and visual effects

---

# Team Members

- Zahra Arshad
- Huda Ali

---

# License

This project was developed for academic and educational purposes only.
