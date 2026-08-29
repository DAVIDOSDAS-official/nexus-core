#pragma once

#include <vector>

#include <nexus/component.hpp>

namespace nexus::system {

// Give every component an unambiguous id.
//
// A package name that exists for more than one architecture must not
// share an id between them. The solver tracks what it has selected by
// id, so two components with the same id are indistinguishable to it:
// once libc6:amd64 is chosen, a later requirement for libc6:i386
// looks already satisfied and the 32-bit build is silently dropped.
//
// Names that appear only once keep their plain form, so ordinary
// single-architecture systems are unaffected.
void qualifyAmbiguousIds(std::vector<Component>& components);

}
