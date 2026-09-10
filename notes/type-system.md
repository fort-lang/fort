# Type System Specification

## Type Categories

### Value Types
- All primitive types
- Fixed-size arrays
- Structs
- Pass by value (full copy)

### Reference Types
- Pointers
- Pass by value (copy pointer, share data)

### Aggregate Types
- Dynamic arrays (hybrid: struct by value, data by reference)

## Primitive Types

### Integer Types
```c
i8   // -128 to 127
i16  // -32,768 to 32,767  
i32  // -2,147,483,648 to 2,147,483,647
i64  // -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807

u8   // 0 to 255
u16  // 0 to 65,535
u32  // 0 to 4,294,967,295
u64  // 0 to 18,446,744,073,709,551,615
```

### Special Integer Types
```c
byte  // Exactly 8 bits, distinct from u8
      // Used for raw binary data
```

### Floating Point Types
```c
f32  // IEEE 754 single precision (32 bits)
f64  // IEEE 754 double precision (64 bits)
```

### Boolean Type
```c
bool  // Can only be 'true' or 'false'
```

### Character Type
```c
char  // ASCII character (8 bits)
```

## Composite Types

### Arrays

#### Fixed-Size Arrays
```c
Type[size]  // Size is part of the type
```
- Size must be compile-time constant
- Different sizes are different types
- `i32[5]` and `i32[10]` are incompatible

#### Dynamic Arrays
```c
Type[]  // Size not part of type
```
- Internally a struct with pointer and capacity
- All dynamic arrays of same element type are compatible

### Structs
```c
struct Name {
    Type1 field1;
    Type2 field2;
    // ...
}
```
- No inheritance
- No methods
- Fields are ordered and packed (with alignment)
- No trailing semicolon after closing brace

### Strings
```c
string  // Built-in type for ASCII strings
```
- Mutable character array
- Length tracked
- Not null-terminated (implementation detail)

## Type Relationships

### Type Equality
Two types are equal if:
- Same primitive type
- Same struct (nominally typed)
- Same array element type and size (for fixed arrays)
- Same array element type (for dynamic arrays)
- Same pointer target type

### Type Compatibility
Types are compatible only if equal. No implicit conversions between:
- Different integer sizes
- Signed and unsigned integers
- Integers and floats
- Arrays of different sizes
- Pointers to different types

## Type Conversion Rules

### No Implicit Conversions
```c
i32 x = 10;
i64 y = x;        // ERROR: no implicit widening
u32 z = x;        // ERROR: no implicit sign change
f64 f = x;        // ERROR: no implicit int-to-float
```

### Explicit Conversions (Syntax TBD)
All conversions must be explicit. Options under consideration:
```c
// Option 1: C-style casts
i64 y = (i64)x;

// Option 2: Keyword style  
i64 y = x as i64;

// Option 3: Function style
i64 y = cast(i64, x);
```

### Conversion Semantics

#### Integer to Integer
- Widening: Sign-extend (signed) or zero-extend (unsigned)
- Narrowing: Truncate high bits
- Sign change: Reinterpret bit pattern

#### Float to Float
- f32 to f64: Precise conversion
- f64 to f32: May lose precision

#### Integer to Float
- May lose precision for large integers
- Rounds to nearest representable value

#### Float to Integer
- Truncates fractional part
- Undefined for out-of-range values (TBD: saturate or trap)

## Const and Mutability

### Const by Default
```c
Type var = value;      // Immutable
mut Type var = value;  // Mutable
```

### Const Propagation (Design Pending)

#### Option 1: Shallow Const
```c
struct Node {
    i32 value;
    Node* next;
}
Node n = {...};  // n.value is const, but n.next->value is mutable
```

#### Option 2: Transitive Const
```c
Node n = {...};  // Everything reachable from n is const
```

### Pointer Const Semantics (Design Pending)
```c
// Syntax not finalized
Type* ptr;              // Mutable pointer to mutable data?
const Type* ptr;        // Mutable pointer to const data?
Type* const ptr;        // Const pointer to mutable data?
const Type* const ptr;  // Const pointer to const data?
```

## Type Inference

### No Type Inference
All types must be explicitly specified:
```c
i32 x = 10;        // Must specify i32
// var x = 10;     // NOT supported
// auto x = 10;    // NOT supported
```

Rationale: Explicit types improve readability and prevent surprises

## Type Aliases (Future)

Potential syntax:
```c
type Length = i32;
type Point2D = Point;
type IntArray = i32[];
```

## Enums (Future)

Potential syntax:
```c
enum Color {
    Red,
    Green,
    Blue
}

enum Result {
    Ok(i32),
    Error(string)
}
```

## Unions (Future)

Potential syntax:
```c
union Value {
    i32 int_val;
    f64 float_val;
    string str_val;
}
```

## Type Safety Features

### Strong Typing
- No implicit conversions
- No type punning (except through explicit unsafe casts)
- Array bounds checking
- No uninitialized variables (locals must be initialized)

### Prevented Errors
- Integer overflow/underflow (behavior TBD)
- Sign-related bugs
- Narrowing conversions
- Type confusion
- Buffer overflows (via bounds checking)

## Generic Types (Future)

Not in initial version, but considering:
```c
struct List<T> {
    T* data;
    u64 capacity;
    u64 length;
}

fn sort<T>(T[] array) { ... }
```

## Type Checking

### Compile-Time Checking
- All types resolved at compile time
- No runtime type information (RTTI)
- No dynamic typing
- No reflection

### Type Errors
All type errors are compile-time errors:
```c
i32 x = "hello";        // ERROR: type mismatch
i32 y = 3.14;          // ERROR: cannot assign f64 to i32
i32[5] a = i32[10];    // ERROR: incompatible array types
```

## Standard Type Operations

### Sizeof Operator (Potential)
```c
u64 size = sizeof(Type);
u64 size = sizeof(expression);
```

### Type Comparison Operators
All types support:
- `==` and `!=` for equality
- Assignment `=`

Numeric types additionally support:
- Comparison: `<`, `<=`, `>`, `>=`
- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Bitwise (integers only): `&`, `|`, `^`, `~`, `<<`, `>>`

## Implementation Notes

### Type