#pragma once

#include <map>
#include <string>
#include <vector>

#include <nexus/component.hpp>
#include <nexus/conflict_detector.hpp>
#include <nexus/requirement.hpp>

namespace nexus {

// Why a particular component was selected. This is the explanation
// trail: every choice the solver made, in the order it made them,
// with the reason attached.
struct SolverStep {
    std::string requirement;     // the requirement being satisfied
    std::string selected;        // the component chosen
    std::string reason;
    std::size_t alternativesConsidered = 1;
    bool wasBacktrackedInto = false;
};

enum class SolverStatus {
    Success,
    Unsatisfiable,
    LimitExceeded
};

struct SolverResult {
    SolverStatus status = SolverStatus::Unsatisfiable;
    std::string reason;

    std::vector<std::string> selected;
    std::vector<SolverStep> steps;

    // The requirement that could not be satisfied, when the status is
    // Unsatisfiable.
    std::string blockedOn;

    std::size_t decisions = 0;
    std::size_t backtracks = 0;
};

struct SolverRequest {
    std::vector<Requirement> requirements;

    // The architecture the top-level requirements are being resolved
    // for. Empty means architecture is not considered at all.
    std::string architecture;

    // capability name -> component id. A preference is a tie-breaker;
    // a requirement is absolute and will never be silently replaced.
    std::map<std::string, std::string> preferred;
    std::map<std::string, std::string> required;
};

// Resolves requirements against a set of available components,
// backtracking when a choice turns out to be unworkable further down.
class Solver {
public:
    explicit Solver(
        std::vector<Component> components,
        ConflictDetector detector = ConflictDetector{}
    );

    SolverResult solve(const SolverRequest& request) const;

    // Upper bound on decisions, to keep a pathological graph from
    // running forever. Exceeding it is reported, never hidden.
    void setDecisionLimit(std::size_t limit);

    std::size_t componentCount() const;

private:
    void buildIndex();

    std::vector<Component> components_;
    ConflictDetector detector_;
    std::size_t decisionLimit_ = 100000;

    // capability name -> indices into components_. Without this the
    // solver rescans every component for every requirement, which is
    // fine for a few thousand installed packages and hopeless for a
    // full archive.
    std::map<std::string, std::vector<std::size_t>> byCapability_;
};

std::string toString(SolverStatus status);

}
