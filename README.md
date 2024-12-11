# ArduinOS - Arduino Operating System

**ArduinOS** is an advanced operating system tailored for the Arduino Uno. It addresses the platform's inherent constraints like single-program execution and the absence of a file system. This OS introduces multitasking, a file management system, and a robust command-line interface.

## Key Features

### Interactive Command Line Interface (CLI)
- Execute commands through the serial terminal for real-time interaction.

### EEPROM-Based File System
- Efficiently manage files with support for up to 10 files.
- Each file name can be up to 12 characters long (including the null terminator).
- View remaining storage and perform actions like storing, retrieving, and deleting files.

### Support for Bytecode Program Execution
- Run programs in a specialized bytecode format.
- Utilize the converter tool to prepare bytecode files for execution.
- Enables the Arduino to multitask by managing multiple programs simultaneously.

### Comprehensive Memory Management
- Store and manipulate variables of the following types:
  - **CHAR** (1 byte)
  - **INT** (2 bytes)
  - **FLOAT** (4 bytes)
  - **STRING** (variable-length, null-terminated)
- Associate variables with processes and free up memory when processes terminate.

### Process Control
- Manage up to 10 concurrent processes with features like:
  - **Start**, **Pause**, **Resume**, and **Terminate**.
- Track process states (running, suspended, terminated), program counters, and allocated variables.

### Stack and Multitasking
- Assign a 32-byte stack to each process.
- Execute multiple processes by alternating between their instructions.

## CLI Commands Overview

| Command                  | Description                                                                 |
|--------------------------|-----------------------------------------------------------------------------|
| `STORE <file> <size>`    | Save a file with the specified name and size in the file system.            |
| `RETRIEVE <file>`        | Load a file from the file system.                                           |
| `ERASE <file>`           | Delete a file from the file system.                                         |
| `FILES`                  | Display the list of stored files.                                           |
| `FREESPACE`              | Show available storage capacity.                                            |
| `RUN <file>`             | Execute a program stored in the file system.                                |
| `LIST`                   | View all active processes.                                                  |
| `SUSPEND <id>`           | Temporarily halt a process by its ID.                                       |
| `RESUME <id>`            | Restart a paused process.                                                   |
| `KILL <id>`              | Terminate a specified process.                                              |

## Preparing Bytecode Programs

Use the included converter tool to format bytecode files for execution.

1. Open a terminal or command prompt.
2. Navigate to the converter tool's directory in the ArduinOS repository.
3. Execute the following command:
   ```bash
   convert <bytecode_file> <output_file> [COMx]
   ```
4. Upload the ArduinOS sketch to your Arduino board.
5. Use the CLI command `FILES` to confirm the presence of the converted file.
6. Run the file with:
   ```bash
   RUN <file_name>
   ```

## Potential Enhancements
Future updates may include the following bonus features:
- **Process Prioritization**: Assign and manage process execution priorities.
- **Shared Memory**: Enable inter-process communication through shared memory.
- **Semaphores**: Introduce semaphores for managing shared resources.
- **Custom Compiler**: Build a compiler to translate C or Python-like code into ArduinOS-compatible bytecode.

---

### Installation Instructions
1. Upload the ArduinOS sketch to your Arduino Uno via the Arduino IDE.
2. Connect your Arduino to a serial terminal application.
3. Utilize the listed CLI commands to interact with the operating system.

---

