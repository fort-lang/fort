# Module System Specification

## Module Organization

### File-Based Modules
- One module per file
- Module name matches filename (without extension)
- File `math.lang` defines module `math`
- No explicit module declaration needed

### Module Structure
```c
// math.lang

// Imports at top
import std::io;

// Declarations
fn add(i32 a, i32 b) -> i32 {
    return a + b;
}

struct Vector {
    f64 x;
    f64 y;
}

// Everything is exported by default (v1)
// Visibility modifiers in future versions
```

## Import System

### Import Syntax

#### Import Entire Module
```c
import math;
// Usage: math.add(1, 2)
```

#### Import Specific Symbols
```c
import math::add;
// Usage: add(1, 2)

import math::{add, subtract, Vector};
// Multiple symbols
```

#### Import with Rename
```c
import math::add as math_add;
import vector::add as vec_add;
// Avoid naming conflicts
```

### No Wildcard Imports
```c
// NOT SUPPORTED:
// import math::*;
```
Rationale: Explicit imports make dependencies clear

### Import Resolution
```c
// Given multiple imports:
import math;
import math::add;

// Both are valid:
add(1, 2);        // Direct import
math.add(1, 2);   // Qualified access
```

## Module Dependencies

### No Circular Dependencies
- Modules form a directed acyclic graph (DAG)
- Circular imports are a compile error
- Enforces clean architecture

### Dependency Example
```c
// vec2.lang
struct Vec2 {
    f64 x;
    f64 y;
}

// vec3.lang
import vec2::Vec2;

struct Vec3 {
    Vec2 xy;
    f64 z;
}

// math.lang
import vec2::Vec2;
import vec3::Vec3;

fn dot2(Vec2 a, Vec2 b) -> f64 {
    return a.x * b.x + a.y * b.y;
}

fn dot3(Vec3 a, Vec3 b) -> f64 {
    return dot2(a.xy, b.xy) + a.z * b.z;
}

## Symbol Resolution

### Name Conflicts
```c
// Error case:
import math::add;
import vector::add;  // ERROR: 'add' already imported

// Solutions:
import math::add as math_add;
import vector::add as vec_add;

// Or use qualified access:
import math;
import vector;
math.add(1, 2);
vector.add(v1, v2);
```

### Symbol Tables
1. Module maintains export table
2. Importing module builds local symbol mapping
3. Direct imports (`::add`) go into unqualified namespace
4. Module imports (`math`) go into qualified namespace

### Type Dependencies
```c
// types.lang
struct Point {
    i32 x;
    i32 y;
}

// graphics.lang
import types::Point;

fn draw(Point p) {  // Can use Point directly
    // ...
}
```

## Compilation Model

### Separate Compilation
- Each module compiles independently
- Generates interface file with:
  - Exported function signatures
  - Exported type definitions
  - Exported constants
- Interface files enable parallel compilation

### Compilation Process
1. Parse import statements
2. Load required module interfaces
3. Build dependency graph
4. Compile in topological order
5. Link modules together

### Module Interface Files
```
// math.interface (auto-generated)
module math {
    fn add(i32, i32) -> i32;
    fn subtract(i32, i32) -> i32;
    struct Vector {
        f64 x;
        f64 y;
    }
}
```

## Module Search Path

### Search Order
1. Current directory
2. Project source directory
3. Standard library path
4. User-specified paths (via compiler flags)

### Path Configuration
```bash
# Compiler flags
langc --module-path=/usr/local/lib/lang
langc --module-path=./vendor
```

## Standard Library Modules

### Core Modules (Planned)
```c
std::io       // Input/output
std::mem      // Memory utilities
std::math     // Mathematical functions
std::string   // String operations
std::array    // Array utilities
std::sys      // System interface
```

### Import Examples
```c
import std::io::{print, println};
import std::math;
import std::string::format;
```

## Module System Rules

### Export Rules (v1)
- All top-level declarations are exported
- No private symbols in first version
- Future: visibility modifiers

### Import Rules
- Imports must appear at top of file
- Cannot import inside functions
- Cannot conditionally import
- Import order doesn't matter (within constraints)

### Naming Conventions
- Module names: lowercase, underscores
- No nested modules initially
- Future: hierarchical modules with `::`

## Future Extensions

### Visibility Modifiers
```c
// Future syntax
pub fn add(i32 a, i32 b) -> i32 { }
priv fn helper() { }
```

### Nested Modules
```c
// Future: math::linear::Vector
// Future: math::trig::sin
```

### Module-Level Constants
```c
// Future
const PI = 3.14159;
export const MAX_SIZE = 1000;
```

### Conditional Compilation
```c
// Future
#[cfg(target_os = "linux")]
import linux::syscall;
```

## Examples

### Example 1: Simple Math Module
```c
// math.lang
fn add(i32 a, i32 b) -> i32 {
    return a + b;
}

fn multiply(i32 a, i32 b) -> i32 {
    return a * b;
}
```

### Example 2: Using Math Module
```c
// main.lang
import math::{add, multiply};

fn main() {
    i32 sum = add(5, 3);
    i32 product = multiply(4, 7);
}
```

### Example 3: Data Structure Module
```c
// list.lang
struct Node {
    i32 value;
    Node* next;
}

struct List {
    Node* head;
    u64 size;
}

fn list_create() -> List {
    List list;
    list.head = 0;  // null
    list.size = 0;
    return list;
}

fn list_append(List* list, i32 value) {
    // Implementation
}
```

### Example 4: Complex Dependencies
```c
// app.lang
import graphics::Window;
import input::{Keyboard, Mouse};
import math::{Vector, Matrix};
import renderer;

fn main() {
    Window w = Window.create(800, 600);
    renderer.init(&w);
    
    // Game loop
    while (w.is_open()) {
        input::update();
        renderer.draw(&w);
    }
}
```

## Design Rationale

### Why File-Based Modules?
- Simple mental model
- Easy to navigate projects
- Natural mapping to filesystem
- Familiar to C programmers

### Why No Textual Include?
- Avoids header duplication
- No include guards needed
- Faster compilation
- Clear dependencies

### Why No Circular Dependencies?
- Forces clean architecture
- Simplifies compilation
- Prevents initialization order issues
- Makes code easier to understand

### Why No Wildcard Imports?
- Explicit dependencies
- Easier to track symbol origin
- Prevents namespace pollution
- Better for tooling

## Implementation Notes

### Module Cache
- Parsed modules cached in memory
- Interface files cached on disk
- Rebuild only changed modules

### Error Messages
```
Error: Circular dependency detected:
  math.lang imports vector.lang
  vector.lang imports physics.lang  
  physics.lang imports math.lang

Error: Symbol 'add' already imported from module 'math'
  at vector.lang:3:import vector::add;
```

### Tooling Support
- Module dependency visualization
- Automatic import organization
- Unused import detection
- Module documentation generation