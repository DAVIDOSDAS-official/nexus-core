# Nexus Core Dependency Resolution

**Status:** Draft  
**Version:** 0.1.0

## 1. Purpose

The dependency resolution subsystem determines all components required
to satisfy a requested Nexus configuration.

It extends the provider resolver by recursively resolving component
dependencies.

The dependency resolver must determine WHAT is required without
performing any system modifications.

## 2. Resolution Flow

The conceptual resolution flow is:

    User Request
        |
        v
    Resolver
        |
        v
    Select Component
        |
        v
    Inspect Dependencies
        |
        v
    Resolve Dependencies
        |
        v
    Validate Dependency Graph
        |
        v
    Generate Resolution Plan

## 3. Component Dependencies

A component may require other components.

Example:

    desktop.kde
        requires:
            display.wayland
            audio.pipewire

The resolver must recursively resolve every required dependency.

## 4. Capability Dependencies

A component may require a capability instead of a specific component.

Example:

    desktop.kde
        requires:
            graphical-session

If multiple components provide the capability, the resolver must use
the provider-selection rules defined by the main resolver specification.

## 5. Recursive Resolution

When resolving a component, the resolver must:

    1. Add the requested component to the resolution context.
    2. Inspect its dependencies.
    3. Resolve each dependency.
    4. Recursively resolve dependencies of those dependencies.
    5. Detect cycles.
    6. Validate the resulting dependency graph.
    7. Produce a deterministic resolution plan.

## 6. Duplicate Dependencies

If multiple components require the same dependency, the dependency
must appear only once in the final installation plan.

Example:

    KDE
      requires:
        audio

    Gaming Profile
      requires:
        audio

If both resolve to PipeWire:

    INSTALL:
      KDE
      Gaming Profile
      PipeWire

PipeWire must not appear twice.

## 7. Dependency Ordering

Dependencies must be installed before the components that require them.

Example:

    KDE
      requires:
        Wayland

The installation order must be:

    1. Wayland
    2. KDE

The resolver must produce an order that satisfies all dependency
relationships.

## 8. Missing Dependencies

If a required dependency cannot be resolved, resolution must fail.

Example:

    KDE
      requires:
        graphical-session

No available component provides:

    graphical-session

Result:

    RESOLUTION FAILED

    Reason:
    Required capability "graphical-session" has no compatible provider.

No installation plan may be produced as successful.

## 9. Dependency Cycles

Dependency cycles are invalid.

Example:

    A
      requires:
        B

    B
      requires:
        C

    C
      requires:
        A

The resolver must detect the cycle and report it.

Example:

    RESOLUTION FAILED

    Dependency cycle detected:

        A → B → C → A

## 10. Self Dependencies

A component requiring itself is invalid.

Example:

    component.a
      requires:
        component.a

The resolver must report this as a dependency cycle.

## 11. Resolution Context

The resolver must maintain a resolution context while recursively
processing dependencies.

The context should track:

- Components currently being resolved
- Components already resolved
- Components selected for installation
- Dependency relationships
- Resolution errors

This prevents infinite recursion and allows cycles to be reported.

## 12. Resolution Plan

A successful dependency resolution produces a resolution plan.

The plan must contain at minimum:

- Components to install
- Components to remove
- Components to configure
- Installation order
- Explanation of major decisions

Example:

    INSTALL:

      display.wayland
      audio.pipewire
      desktop.kde

    ORDER:

      1. display.wayland
      2. audio.pipewire
      3. desktop.kde

## 13. Existing Components

The resolver must eventually distinguish between:

- Already installed components
- Components that must be installed
- Components that must be updated
- Components that must be removed

For the initial implementation, existing system state may be
represented by an abstract input rather than the real operating system.

## 14. User Intent

Explicit user requirements must retain the priority defined by the
main resolver specification.

Dependency resolution must not silently replace an explicitly
required provider.

If an explicit requirement makes the configuration impossible,
resolution must fail with an explanation.

## 15. Determinism

Given identical:

- Component metadata
- Configuration
- Hardware state
- Repository state
- Resolver version

the resolver must produce the same resolution result and installation
order.

## 16. Safety

Dependency resolution must never:

- Install software
- Remove software
- Modify system files
- Modify boot configuration
- Modify package databases
- Change hardware configuration

The resolver only produces a proposed plan.

A separate execution subsystem will eventually apply the plan.

## 17. Dry Run

Dependency resolution must support dry-run operation.

Example:

    nexus resolve --dry-run

Expected behavior:

    Requested:
      desktop.kde

    Dependencies:
      display.wayland
      audio.pipewire

    Proposed installation order:

      1. display.wayland
      2. audio.pipewire
      3. desktop.kde

    No system changes have been made.

## 18. Explainability

The resolver should be able to explain dependency decisions.

Example:

    Why is PipeWire included?

    Because:
      desktop.kde requires audio.
      PipeWire provides audio.
      PipeWire satisfies the requested capability.

## 19. Future Extensions

Future versions may support:

- Version constraints
- Optional dependencies
- Recommendations
- Hardware-specific dependencies
- Alternative dependency paths
- Package-level dependencies
- Cross-distribution dependencies
- Isolated environments
- Conflict resolution
- Optimization
- Parallel installation planning

## 20. Design Principle

Dependency resolution must answer:

    "What must exist for this configuration to work?"

before any system modification is attempted.

The resolver should favor:

- Correctness
- Determinism
- Explainability
- Safety
- User control

over automatic convenience.
