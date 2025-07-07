# Computer-Architecture-simulator
A basic computer architecture simulator written in C. It reads a custom instruction set from a file and simulates execution on a simple processor model. Developed as a university project to explore CPU cycles, registers, and instruction-level behavior.
# Computer Architecture Simulator – ca.c

This is a Computer Architecture simulation project developed in C. It simulates a custom processor executing instructions read from an external file, modeling basic CPU behaviors like instruction fetch, decode, and execute.

## 💡 Features

- Custom instruction set parsing
- Simulation of instruction cycles
- Simple processor model written in C
- Accepts input from `program.txt`

## 📂 Files

| File           | Description                        |
|----------------|------------------------------------|
| `ca.c`         | Main source code for the simulator |
| `program.txt`  | Input instruction file             |
| `sim`          | Compiled executable (ignored)      |

## ▶️ How to Run

```bash
gcc ca.c -o sim       # Compile
./sim program.txt     # Run simulation with input
