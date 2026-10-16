# Random generators

OpenBMH uses two explicit stateful generators in `src/game/random.hpp`. Frontends do not create gameplay randomness. Save and rollback operations must preserve the relevant generator state and call order.

## General generator

The compatibility generator uses a 32-bit unsigned linear recurrence:

```text
seed = seed * 0x41C64E6D + 0x3039        (modulo 2^32)
r = (seed >> 16) & 0x7FFF
n = hi - lo + 1
product = signed32(unsigned32(r) * unsigned32(n))
result = lo + product / 32768
```

The product wraps at 32 bits and signed division truncates toward zero. For an empty range it returns the lower bound without drawing. A run's initial seed is supplied explicitly by the session/CLI when reproducibility is required.

## Game generator

The second stream has a separate recurrence and can be reseeded by defined game operations:

```text
seed = signed32(unsigned32(seed) * 16 + 0xA7) % 4000000
product = signed32(unsigned32(seed) * unsigned32(hi - lo + 1))
result = product / 4000000 + lo
```

Signed division truncates toward zero; overflow in the indicated products wraps at 32 bits before signed interpretation. Replacing this with floating-point arithmetic or a standard-library distribution changes results. Negative intermediate values must retain their specified semantics.

Observing or rendering a value should not draw random numbers unless opening that actual game operation requires it. Transactional failures restore both streams with the game state. A seed alone is not a complete replay specification: engine version, data, options and command order also matter.
