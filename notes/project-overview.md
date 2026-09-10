# C-Like Language Design Project

## Project Intent

This project aims to design a new systems programming language that takes C as its foundation and makes targeted improvements to eliminate common sources of bugs while maintaining C's simplicity and directness. The language is explicitly not trying to be as safe as Rust or as feature-rich as C++, but rather a "slightly better C" that a C programmer could learn in an afternoon.

## Design Philosophy

### Core Principles
1. **Familiarity First**: Maintain C-like syntax and semantics where possible
2. **Explicit Over Implicit**: No hidden behaviors, automatic conversions, or magic
3. **Safety Without Complexity**: Simple improvements that eliminate entire bug classes
4. **Manual Control**: Programmer retains full control over memory and performance
5. **Minimal Runtime**: No garbage collection, no hidden allocations

### Non-Goals
- Not trying to prevent all memory safety issues
- Not adding advanced type system features (generics, traits, etc.) initially
- Not targeting beginners - assumes C programming knowledge
- Not prioritizing compile-time guarantees over simplicity

## Language Components

### 1. Core Language (`core-language.md`)
- Type system and primitives
- Variables and mutability
- Operators and expressions
- Control flow constructs
- Functions and calling conventions

### 2. Memory Model (`memory-model.md`)
- Allocation and deallocation
- Pointer semantics
- Array implementation
- Stack vs heap
- Memory safety boundaries

### 3. Type System (`type-system.md`)
- Primitive types
- Composite types (structs, arrays)
- Type conversion rules
- Const/mutability system
- Future: enums, unions, aliases

### 4. Module System (`module-system.md`)
- File organization
- Import/export mechanism
- Symbol resolution
- Compilation model
- Visibility (future)

### 5. Standard Library (`stdlib.md`)
- Built-in functions
- Core data structures
- I/O operations
- String handling
- C interop layer

### 6. Toolchain (`toolchain.md`)
- Compiler architecture
- Build system
- Debugger support
- Package management (future)
- Documentation tools

## Development Approach

### Phase 1: Core Design (Current)
- Define syntax and semantics
- Establish type system
- Design memory model
- Create module system

### Phase 2: Implementation Planning
- Choose implementation language
- Design compiler architecture
- Plan standard library
- Define C FFI mechanism

### Phase 3: Prototype
- Implement minimal compiler
- Basic standard library
- Simple test programs
- Performance benchmarks

### Phase 4: Refinement
- Syntax adjustments based on usage
- Optimize common patterns
- Expand standard library
- Tooling improvements

## Key Innovations

### Safety Improvements Over C
1. **Const by default** - Prevents accidental mutation
2. **No implicit conversions** - Eliminates surprising behavior
3. **Bounds-checked arrays** - Prevents buffer overflows
4. **No pointer arithmetic** - Reduces memory corruption
5. **Required initialization** - No undefined values
6. **No goto** - Cleaner control flow
7. **Switch without fallthrough** - Eliminates common bug

### Quality of Life Improvements
1. **Module system** - Better code organization than headers
2. **Built-in bool type** - With `true`/`false` keywords
3. **Simplified struct syntax** - No redundant keywords
4. **Range-based for loops** - Cleaner iteration
5. **Type-safe memory allocation** - `new(Type)` instead of malloc

## Design Status

### Completed Decisions
- ✅ Basic type system
- ✅ Mutability model (const by default, `mut` keyword)
- ✅ Array semantics (embedded length, bounds checking)
- ✅ Control flow (C-like with improvements)
- ✅ Module system basics
- ✅ Memory allocation approach

### Pending Decisions
- ⏳ Explicit cast syntax
- ⏳ Overflow behavior
- ⏳ Error handling mechanism
- ⏳ Const transitivity rules
- ⏳ String encoding (UTF-8 support)
- ⏳ Generic programming approach

### Future Considerations
- 🔮 Compile-time computation
- 🔮 Macro system or metaprogramming
- 🔮 Concurrency primitives
- 🔮 Package management
- 🔮 Standard library scope

## Success Criteria

The language will be considered successful if:
1. C programmers can learn it in a day
2. Common C bugs are structurally prevented
3. Performance is within 10% of equivalent C code
4. Real systems software can be written in it
5. The compiler is simple enough for one person to understand

## References and Inspiration

### Direct Influences
- **C**: Base syntax and system programming model
- **Rust**: Mutability model, no implicit conversions
- **Go**: Simple module system, explicit error handling
- **Zig**: Compile-time computation, no hidden allocations

### Anti-Patterns to Avoid
- C++ complexity creep
- Java's verbose boilerplate
- JavaScript's implicit conversions
- Python's runtime overhead