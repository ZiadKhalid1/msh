# msh - A Minimal Unix-Like Shell in C

`msh` is a minimalist Unix-like shell implemented in C. It focuses on demonstrating how a shell works under the hood, including command parsing, variable management, input/output redirection, and process control.

This project is built for educational purposes to strengthen understanding of Unix system programming and shell behavior.

---

## 💑 Features

### ✅ Built-in Commands
- `cd`: Change directory  
- `pwd`: Print current working directory  
- `export`: Export environment variables  

---

### ✅ Variable Handling
- **Local Variables**  
```bash
VAR=value
```

- **Export to Environment**  
```bash
export VAR=value
```

- **Variable Expansion**  
```bash
echo $VAR
```

---

### ✅ Redirection Support
`msh` supports standard Unix redirections:

| Symbol | Purpose         |
|--------|-----------------|
| `>`    | Standard output |
| `<`    | Standard input  |
| `2>`   | Standard error  |

**Examples:**
```bash
ls > output.txt
cat < input.txt
ls 2> errors.log
```

---

### ✅ Command Execution
- Executes external programs using `fork()` and `execvp()`
- Correctly passes environment variables to child processes

---

### ✅ Readline Integration
- Input history
- Editable input lines (arrow keys, backspace, etc.)

---

## ⚙️ Building msh

### 🔧 Requirements
- GCC (or Clang)
- GNU Readline Development Library  
  (Ubuntu/Debian: `sudo apt install libreadline-dev`)

---

### 🏗️ Compilation
A simple `Makefile` is included. To build the project, run:

```bash
make
```

This will generate the executable:
```bash
./msh
```

---

## 🚀 Usage Example
Start the shell with:
```bash
./msh
```

Example session:
```bash
msh> pwd
/home/ziad
msh> MYVAR=hello
msh> echo $MYVAR
hello
msh> cd /tmp
msh> pwd
/tmp
msh> ls > output.txt
msh> cat < output.txt
...
msh> exit
```

---

## 🛑 Limitations
This project intentionally avoids advanced shell features to focus on core concepts. Not supported:
- Pipelines (`|`)
- Logical operators (`&&`, `||`)
- Background processes (`&`)
- Complex quoting or escape sequences
- Scripting or control flow

---

## 📂 Project Structure (Single-File Version)
```text
msh.c       # Entire shell source code
Makefile    # Build instructions
README.md   # Project documentation
```

---

## 🎯 Learning Objectives
This project demonstrates:
- Input parsing via finite-state machines (FSM)
- Manual variable substitution and management
- I/O redirection using `dup2()`
- Unix process control (`fork()`, `execvp()`, `waitpid()`)
- Integration with GNU Readline

---

## 📜 License
Licensed under the **MIT License**.  
You are free to use, modify, and distribute this project for educational and personal purposes.

---

## 🤝 Contribution
This is a personal educational project. Contributions are not expected but are welcome if they maintain the spirit of simplicity and learning.

