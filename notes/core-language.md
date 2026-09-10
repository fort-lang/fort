# Core Language Specification

## Variables and Mutability

### Declaration Syntax
```c
Type name = value;      // Immutable by default
mut Type name = value;  // Mutable with 'mut' keyword
```

### Const by Default
- All variables are immutable unless explicitly marked `mut`
- Applies to all types: primitives, composites, pointers
- Prevents accidental mutation bugs

### Examples
```c
i32 x = 10;         // Cannot be modified
mut i32 y = 20;     // Can be modified
y = 30;             // OK
x = 40;             // ERROR: cannot modify immutable variable
```

## Primitive Types

### Integer Types
| Type | Size | Range |
|------|------|-------|
| `i8`  | 8 bits  | -128 to 127 |
| `i16` | 16 bits | -32,768 to 32,767 |
| `i32` | 32 bits | -2³¹ to 2³¹-1 |
| `i64` | 64 bits | -2⁶³ to 2⁶³-1 |
| `u8`  | 8 bits  | 0 to 255 |
| `u16` | 16 bits | 0 to 65,535 |
| `u32` | 32 bits | 0 to 2³²-1 |
| `u64` | 64 bits | 0 to 2⁶⁴-1 |

### Floating Point Types
- `f32` - 32-bit IEEE 754 single precision
- `f64` - 64-bit IEEE 754 double precision

### Other Primitives
- `bool` - Boolean type with `true` and `false` keywords
- `byte` - Exactly 8 bits, distinct from `u8`
- `char` - ASCII character

## Operators

### Arithmetic Operators
```c
+   // Addition
-   // Subtraction  
*   // Multiplication
/   // Division
%   // Modulo
```
- No implicit type promotion
- Operands must have matching types
- Division by zero behavior: (TBD - trap vs defined result)

### Comparison Operators
```c
==  // Equal
!=  // Not equal
<   // Less than
<=  // Less than or equal
>   // Greater than
>=  // Greater than or equal
```

### Logical Operators
```c
&&  // Logical AND (short-circuit)
||  // Logical OR (short-circuit)
!   // Logical NOT
```

### Bitwise Operators
```c
&   // Bitwise AND
|   // Bitwise OR
^   // Bitwise XOR
~   // Bitwise NOT
<<  // Left shift
>>  // Right shift (arithmetic for signed, logical for unsigned)
```

### Assignment Operators
```c
=   // Simple assignment
+=  // Add and assign
-=  // Subtract and assign
*=  // Multiply and assign
/=  // Divide and assign
%=  // Modulo and assign
&=  // Bitwise AND and assign
|=  // Bitwise OR and assign
^=  // Bitwise XOR and assign
<<= // Left shift and assign
>>= // Right shift and assign
```

## Control Flow

### Conditional Statements
```c
if (condition) {
    // code
}

if (condition) {
    // code
} else {
    // code
}

if (condition1) {
    // code
} else if (condition2) {
    // code
} else {
    // code
}
```

### Loop Constructs

#### While Loop
```c
while (condition) {
    // code
}
```

#### Do-While Loop
```c
do {
    // code
} while (condition);
```

#### For Loop
```c
for (init; condition; increment) {
    // code
}

// Example
for (i32 i = 0; i < 10; i++) {
    // code
}
```

#### Range-Based For Loop
```c
for (Type element : array) {
    // iterate over values
}

// Example
i32[] numbers = {1, 2, 3, 4, 5};
for (i32 n : numbers) {
    print(n);
}
```

### Switch Statement
```c
switch (expression) {
    case value1:
        // code
        // implicit break - no fallthrough
    case value2:
        // code
    default:
        // code
}
```
- No explicit `break` needed
- No fallthrough between cases
- Cases must be compile-time constants

### Control Keywords
- `break` - Exit loop or switch
- `continue` - Skip to next iteration
- `return` - Return from function
- No `goto` - Removed for cleaner control flow

## Functions

### Function Declaration
```c
ReturnType name(Type1 param1, Type2 param2) {
    // body
    return value;
}
```

### Void Functions
```c
void print_number(i32 n) {
    // no return statement needed
}
```

### Function Examples
```c
i32 add(i32 a, i32 b) {
    return a + b;
}

bool is_even(i32 n) {
    return n % 2 == 0;
}

void swap(i32* a, i32* b) {
    i32 temp = *a;
    *a = *b;
    *b = temp;
}
```

### Function Pointers
```c
// Declaration
ReturnType (*name)(ParamType1, ParamType2);

// Example
i32 (*operation)(i32, i32);
operation = &add;
i32 result = operation(5, 3);
```

### Parameter Passing
- Primitives: Always pass by value
- Structs: Pass by value (full copy)
- Arrays:
  - Fixed arrays: Pass by value (copy all elements)
  - Dynamic arrays: Pass by value (copy struct, share heap data)
- Pointers: Pass by value (copy pointer, share pointed-to data)

## Comments
```c
// Single-line comment

/* 
   Multi-line
   comment
*/
```

## Literals

### Integer Literals
```c
42      // Decimal
0x2A    // Hexadecimal
0o52    // Octal
0b101010 // Binary
```

### Floating Point Literals
```c
3.14159
2.71e-3
1.0f    // f32 literal
1.0     // f64 literal by default
```

### Character Literals
```c
'a'
'\n'    // Newline
'\t'    // Tab
'\\'    // Backslash
'\''    // Single quote
```

### String Literals
```c
"Hello, World!"
"Line 1\nLine 2"
```

### Boolean Literals
```c
true
false
```

## Type Conversion

### No Implicit Conversions
- No automatic type promotion
- No implicit narrowing or widening
- All conversions must be explicit

### Explicit Cast Syntax (TBD)
Options under consideration:
```c
// Option 1: C-style
i32 x = (i32)y;

// Option 2: Keyword style
i32 x = y as i32;

// Option 3: Function style
i32 x = cast(i32, y);
```

## Operator Precedence (Highest to Lowest)

1. Primary: `()` `[]` `.` `->`
2. Unary: `!` `~` `-` `*` `&` 
3. Multiplicative: `*` `/` `%`
4. Additive: `+` `-`
5. Shift: `<<` `>>`
6. Relational: `<` `<=` `>` `>=`
7. Equality: `==` `!=`
8. Bitwise AND: `&`
9. Bitwise XOR: `^`
10. Bitwise OR: `|`
11. Logical AND: `&&`
12. Logical OR: `||`
13. Assignment: `=` `+=` `-=` etc.

## Keywords

### Reserved Words
```
bool break byte case char continue default do
else false f32 f64 for i8 i16 i32 i64
if import mut new del return string struct
switch true u8 u16 u32 u64 void while
```

### Potential Future Keywords
```
as async await const enum match trait type union yield
```