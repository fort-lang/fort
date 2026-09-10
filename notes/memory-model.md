# Memory Model Specification

## Memory Regions

### Stack Memory
- Function local variables
- Function parameters
- Fixed-size arrays
- Return addresses
- Automatically cleaned up on scope exit

### Heap Memory
- Dynamically allocated memory via `new`
- Dynamic arrays data storage
- Explicitly allocated structs
- Manual cleanup required via `del`

## Allocation

### Stack Allocation
```c
// Automatic for local variables
i32 x = 10;
Point p = {1, 2};
i32[5] arr = {1, 2, 3, 4, 5};
```
- No explicit allocation needed
- Cleaned up automatically
- Size must be known at compile time

### Heap Allocation

#### Single Object Allocation
```c
Type* ptr = new(Type);
```
- Returns pointer to allocated object
- Memory is uninitialized
- Must manually initialize fields

#### Array Allocation
```c
Type[] arr = new(Type[n]);
```
- Allocates array of `n` elements
- Memory is uninitialized
- Returns dynamic array struct

#### Examples
```c
// Allocate single struct
Point* p = new(Point);
p->x = 10;
p->y = 20;

// Allocate array
i32[] numbers = new(i32[100]);
numbers[0] = 42;
```

## Deallocation

### Syntax
```c
del(ptr);
```
- Works for any heap-allocated memory
- Single objects or arrays
- Pointer becomes invalid after `del`
- No automatic cleanup - manual `del` required

### Examples
```c
Point* p = new(Point);
// ... use p ...
del(p);
// p is now dangling - using it is undefined behavior

i32[] arr = new(i32[100]);
// ... use arr ...
del(arr);
```

## Pointers

### Pointer Types
```c
Type*  // Pointer to Type
```

### Pointer Operations
```c
&variable   // Address-of: get pointer to variable
*pointer    // Dereference: access pointed-to value
ptr->field  // Arrow: access struct field through pointer
```

### Pointer Restrictions
- **No pointer arithmetic**: Cannot do `ptr++`, `ptr + n`, `ptr[i]`
- **No pointer indexing**: Must dereference then index
- **Reference-only**: Pointers only for indirection
- **No null keyword**: Use 0 for null pointer (for now)

### Pointer Examples
```c
i32 x = 10;
i32* ptr = &x;
i32 value = *ptr;  // value = 10

Point p = {1, 2};
Point* pp = &p;
i32 x_coord = pp->x;  // x_coord = 1
```

### Pointer and Const
```c
i32* ptr;           // Pointer to mutable i32
const i32* ptr;     // Pointer to immutable i32 (TBD)
i32* const ptr;     // Immutable pointer to mutable i32 (TBD)
```
- Exact const pointer semantics still being determined
- Considering transitive const vs shallow const

## Arrays

### Array Memory Layout

#### Fixed Arrays
```c
i32[5] arr;
```
Memory layout:
```
[elem0][elem1][elem2][elem3][elem4]
```
- Contiguous memory on stack
- Size embedded in type
- No separate metadata

#### Dynamic Arrays
```c
i32[] arr;
```
Implemented as:
```c
struct {
    i32* data;      // Pointer to heap memory
    u64 capacity;   // Number of elements allocated
}
```
- Struct on stack, data on heap
- Capacity tracked, not current length
- User must track actual element count

### Array Allocation Examples
```c
// Stack allocation - fixed array
i32[5] stack_arr = {1, 2, 3, 4, 5};

// Heap allocation - dynamic array
i32[] heap_arr = new(i32[100]);
// heap_arr.data points to 100 i32s
// heap_arr.capacity = 100
```

### Array Assignment Semantics

#### Fixed Arrays
```c
i32[5] arr1 = {1, 2, 3, 4, 5};
i32[5] arr2 = arr1;  // Deep copy - all elements copied
```

#### Dynamic Arrays
```c
i32[] arr1 = new(i32[10]);
i32[] arr2 = arr1;  // Shallow copy - shares heap data
// Both arr1 and arr2 point to same heap memory
```

### Array Bounds Checking
- All array accesses are bounds checked
- Out-of-bounds access causes runtime error
- Check against capacity for dynamic arrays
- Check against size for fixed arrays

## Structs

### Struct Memory Layout
```c
struct Point {
    i32 x;
    i32 y;
}
```
Memory layout (subject to alignment):
```
[x: 4 bytes][y: 4 bytes]
```

### Struct Allocation
```c
// Stack allocation
Point p1 = {10, 20};

// Heap allocation
Point* p2 = new(Point);
p2->x = 10;
p2->y = 20;
```

### Struct Assignment
```c
Point p1 = {10, 20};
Point p2 = p1;  // Deep copy - all fields copied
```

## Memory Safety Features

### Bounds Checking
- All array accesses checked at runtime
- Prevents buffer overflows
- Small runtime cost for safety

### No Pointer Arithmetic
- Eliminates large class of memory errors
- Cannot accidentally walk off arrays
- Pointers are purely for indirection

### Initialization Requirements
- Local variables must be initialized
- Heap memory starts uninitialized (design choice)
- No random garbage values for stack variables

## Memory Management Best Practices

### Ownership Patterns
```c
// Clear ownership
Type* create_thing() {
    Type* t = new(Type);
    // ... initialize ...
    return t;  // Caller owns memory
}

void use_thing() {
    Type* t = create_thing();
    // ... use t ...
    del(t);  // Clean up when done
}
```

### RAII Pattern (Manual)
```c
void process_data() {
    i32[] data = new(i32[1000]);
    
    // ... process data ...
    // Multiple return paths need cleanup
    
    if (error_condition) {
        del(data);
        return;
    }
    
    // ... more processing ...
    
    del(data);  // Don't forget!
}
```

### Common Pitfalls
```c
// Dangling pointer
Point* p = new(Point);
del(p);
p->x = 10;  // ERROR: use after free

// Memory leak
for (i32 i = 0; i < 100; i++) {
    i32[] arr = new(i32[10]);
    // Missing del(arr)
}

// Double free
Point* p = new(Point);
del(p);
del(p);  // ERROR: double free
```

## Future Considerations

### Potential Additions
- Slice types for array views
- Arena allocators
- Stack-based dynamic arrays (small buffer optimization)
- Defer statement for cleanup
- Basic leak detection in debug builds

### Alignment Control
- Currently using natural alignment
- May add alignment attributes
- Packed structs for network protocols

### Debug Features
- Memory usage tracking
- Leak detection
- Use-after-free detection
- Bounds checking levels (debug vs release)