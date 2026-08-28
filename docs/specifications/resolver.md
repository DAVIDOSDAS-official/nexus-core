# Nexus Core Resolver

**Status:** Draft
**Version:** 0.1.0

## 1. Purpose

The Nexus Core Resolver determines whether a requested system
configuration can be constructed from the available components.

The resolver converts:

    User Intent
        |
        v
    Configuration
        |
        v
    Dependency Resolution
        |
        v
    Conflict Detection
        |
        v
    Hardware Validation
        |
        v
    Installation Plan

The resolver must never directly install or remove software.

Its responsibility is to determine WHAT must change.

## 2. Responsibilities

The resolver is responsible for:

- Resolving component dependencies
- Resolving capability requirements
- Detecting conflicts
- Evaluating version constraints
- Evaluating hardware requirements
- Selecting compatible alternatives
- Detecting impossible configurations
- Producing an execution plan
- Explaining why a configuration is valid or invalid

The resolver is NOT responsible for:

- Installing packages
- Removing packages
- Modifying system files
- Downloading software
- Booting the system
- Managing hardware directly

Those responsibilities belong to other Nexus Core subsystems.

## 3. Inputs

The resolver receives:

- Desired configuration
- Available components
- Current system state
- Hardware capabilities
- Repository information
- Component metadata

Conceptually:

    Configuration
          +
    Component Database
          +
    Current State
          +
    Hardware
          |
          v
       Resolver

## 4. Outputs

A successful resolution produces an installation plan.

Example:

    INSTALL:
      wayland
      kde-plasma
      pipewire

    REMOVE:
      gnome

    CONFIGURE:
      desktop = kde
      audio = pipewire

A failed resolution produces a structured explanation.

Example:

    RESOLUTION FAILED

    Component:
      example.component

    Conflict:
      example.component conflicts with
      example.other-component

    Reason:
      Both components provide incompatible
      implementations of the same required capability.

## 5. Dependency Resolution

Components may require other components or capabilities.

Example:

    KDE
      requires:
        graphical-session

    Wayland
      provides:
        graphical-session

The resolver may therefore satisfy KDE's requirement through Wayland.

The resolver should prefer the most appropriate compatible provider.

## 6. Capability-Based Resolution

The resolver should prefer capabilities over hard-coded component
dependencies where possible.

Example:

    Application A
      requires:
        audio

Instead of requiring a specific audio implementation, the resolver
may select any compatible component providing:

    audio

Possible providers could include:

- PipeWire
- Another supported audio system

The exact implementation is selected according to compatibility,
user preference, hardware, and system policy.

## 7. Version Constraints

Dependencies may specify version requirements.

Examples:

    requires:
      linux >= 6.10

or:

    requires:
      library >= 2.0
      library < 3.0

The resolver must evaluate version constraints before producing an
installation plan.

## 8. Conflicts

Components may explicitly declare conflicts.

Example:

    component.a
      conflicts:
        component.b

If both are selected, resolution fails unless a valid alternative
exists.

The resolver must not silently remove a user-selected component.

## 9. Alternatives

Multiple components may provide the same capability.

Example:

    desktop

could be provided by:

- KDE Plasma
- GNOME
- XFCE
- COSMIC
- another supported desktop

The resolver may select an alternative when:

- The user has not explicitly selected an implementation
- The selected implementation is incompatible
- A dependency requires the capability
- Hardware constraints prevent the preferred implementation

Explicit user choices must have higher priority than automatic
alternatives unless the requested configuration is impossible.

## 10. User Intent Priority

The resolver must distinguish between:

- Required choices
- Preferred choices
- Optional choices
- Automatic choices

Priority:

    Explicit user requirement
            >
    User preference
            >
    Profile preference
            >
    Nexus default

The resolver should never silently override an explicit user
requirement.

## 11. Hardware Compatibility

The resolver receives hardware capabilities from the hardware subsystem.

Example:

    GPU:
      vendor: AMD
      acceleration: supported

The resolver can then determine whether a requested component is
compatible with the detected hardware.

Hardware incompatibility must produce a clear explanation.

## 12. Distribution Ecosystems

Nexus Core may integrate technologies originating from different Linux
ecosystems.

Examples include:

- Debian
- Ubuntu
- Fedora
- Arch
- Gentoo
- openSUSE
- Kali
- Other compatible ecosystems

Nexus must NOT assume that components from these ecosystems are
automatically interchangeable.

The resolver must evaluate:

- Dependencies
- ABI compatibility
- Package format
- Library versions
- Configuration requirements
- System integration
- Conflicts
- Licensing

A component may be used directly, adapted, isolated, or rejected
depending on compatibility.

## 13. Isolation

When components cannot safely coexist in the base system but can
operate independently, Nexus may use isolation mechanisms.

Possible mechanisms include:

- Containers
- Sandboxing
- Namespaces
- Separate environments
- Virtual machines
- Compatibility layers

Isolation should be preferred over unsafe mixing of incompatible
system components.

## 14. Resolution Process

The resolver follows a conceptual process:

    1. Read desired configuration
    2. Read current system state
    3. Read available components
    4. Read hardware capabilities
    5. Expand required capabilities
    6. Resolve dependencies
    7. Evaluate version constraints
    8. Detect conflicts
    9. Evaluate hardware compatibility
    10. Select alternatives when allowed
    11. Generate proposed changes
    12. Validate the final configuration
    13. Produce an installation plan

## 15. Determinism

Given the same:

- Configuration
- Component metadata
- Hardware state
- Repository state
- Resolver version

the resolver should produce the same result.

Non-deterministic behavior should be explicitly documented.

## 16. Explainability

Every automatic resolver decision should be explainable.

Example:

    Why was PipeWire selected?

    Because:
      - Audio capability was requested.
      - PipeWire provides audio.
      - PipeWire is compatible with the selected desktop.
      - No explicit alternative was selected.

The user should be able to inspect resolver decisions.

## 17. Dry Run

The resolver must support a dry-run mode.

Example:

    nexus resolve --dry-run

Output:

    Proposed changes:

    + Install KDE Plasma
    + Install Wayland
    + Install PipeWire
    - Remove GNOME

    No changes have been made.

## 18. Safety

Resolution must happen BEFORE system modification.

The resolver must never directly modify the operating system.

The output must be passed to another subsystem responsible for safely
applying the plan.

## 19. Recovery Integration

Before critical changes are applied, Nexus should create an appropriate
recovery point.

Conceptually:

    Resolve
       |
       v
    Validate
       |
       v
    Recovery Point
       |
       v
    Apply Plan
       |
       v
    Verify
       |
       +---- Success --> Commit
       |
       +---- Failure --> Rollback

## 20. Future Optimization

The resolver may eventually optimize for:

- Disk usage
- Performance
- Security
- Stability
- Power consumption
- Startup time
- User preferences

Optimization must never violate explicit user requirements.

## 21. Design Principle

The resolver is the decision engine of Nexus Core.

It determines:

    "Can the user's requested operating system exist?"

and, if it can:

    "What changes are necessary to construct it?"

It must favor correctness, safety, transparency, and user control over
automatic convenience.

## 22. Provider Selection

When multiple components provide the same capability, the resolver must
evaluate all compatible providers before selecting one.

The resolver must NOT select a provider merely because it appears first
in the component database.

Provider selection follows this priority:

    1. Explicit user requirement
    2. Explicit user preference
    3. Hardware compatibility
    4. Required version constraints
    5. System/profile preference
    6. Nexus default policy

An explicit user requirement must not be replaced by another provider
unless the requested provider makes the configuration impossible.

### Example

Available providers:

    PipeWire
      provides:
        - audio

    PulseAudio
      provides:
        - audio

    JACK
      provides:
        - audio

If the user explicitly selects:

    audio = pipewire

the resolver must select PipeWire.

If the user does not select a provider, the resolver may evaluate all
compatible providers and select one according to the active system policy.

### Provider Selection Result

The resolver should identify:

- Requested capability
- Candidate providers
- Rejected providers
- Rejection reasons
- Selected provider
- Selection reason

Example:

    Capability:
      audio

    Candidates:
      - PipeWire
      - PulseAudio
      - JACK

    Selected:
      PipeWire

    Reason:
      PipeWire is the preferred compatible provider for the active
      system profile.

### Ambiguity

If multiple providers are equally valid and no preference can
distinguish them, the resolver should not make an arbitrary choice.

Instead it should return an explicit ambiguous-resolution result.

Example:

    RESOLUTION REQUIRES USER DECISION

    Capability:
      audio

    Compatible providers:
      - PipeWire
      - PulseAudio

    Reason:
      No provider has sufficient priority to be selected automatically.

The user may then explicitly select a provider.

### Determinism

Provider selection must be deterministic.

Given identical:

- User configuration
- Component metadata
- Hardware state
- Repository state
- System profile
- Resolver version

the same provider must be selected.

### Explainability

Every provider selection must have a machine-readable reason.

The CLI and GUI should eventually be able to display this reason to
the user.

The resolver must never hide an automatic provider selection.

### Hard Constraints

Hardware compatibility, version constraints, and explicit conflicts are
hard constraints rather than selection preferences.

A provider that fails a hard constraint must be rejected before provider
ranking occurs.

The resolver must not compensate for an incompatible provider by assigning
it a lower priority or silently selecting a different provider.

If the user explicitly requested the incompatible provider, resolution
must fail with an explanation rather than silently replacing it.

Provider ranking occurs only among providers that satisfy all applicable
hard constraints.

## 23. Resolution Requests

A resolution request represents a user's or subsystem's request for a
specific capability.

A request may optionally specify:

- Preferred provider
- Required provider

### Required Provider

A required provider is an explicit requirement.

Example:

    capability:
      audio

    required-provider:
      audio.pipewire

The resolver must select the required provider if it is available and
compatible.

If the required provider cannot satisfy the request, resolution must
fail.

The resolver must not silently substitute another provider.

### Preferred Provider

A preferred provider expresses a user preference rather than a hard
requirement.

Example:

    capability:
      audio

    preferred-provider:
      audio.pipewire

The resolver should select the preferred provider when it is compatible.

If the preferred provider is unavailable or incompatible, the resolver
may select another compatible provider.

The result must explain that the preferred provider could not be used.

### Priority

Provider selection follows:

    Required provider
        >
    Preferred provider
        >
    Other compatible providers

Hard constraints are evaluated before preference ranking.

Explicit requirements must never be silently overridden.
