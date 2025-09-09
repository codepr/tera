Unnamed MQTT
============

## Specs

Lightweight and fast, mainly targeting small systems (think embedded, small devices and low resources tech).

### Built like a videogame

- Library first, with a possiblity of including it in a built-in server architecture
    - UDP vs TCP; official specs would require TCP
- Cache friendly design, built like a videogame
    - Data oriented design first - this approach will come with the drawback of
      constraining the maximum number of clients and message size to a fixed
      (likely low) value to ensure predictability and performances
        - Use of `SoAs` (Structure of Arrays) to ensure cache locality and `SIMD` optimization possibility
        - Example, instead of hgaving a single struct for each client that contains all its data, use arrays of related data
            - Client IDs
            - Session states
            - Message topics
            - Message IDs
        - Avoid runtime allocations: either use the stack or pre-allocation of memory at boot through a `pool allocator` or `bump arena`
