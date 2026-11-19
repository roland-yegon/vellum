# ADR-000: Architecture Decision Records

- **Status:** Accepted
- **Date:** 2026-11-19

## Decision

Architectural decisions are recorded as individual, numbered Markdown files in
`docs/architecture/`. Each record contains, at minimum: **Decision**, **Context**,
**Alternatives considered**, **Consequences**, and **Conditions that justify
revisiting**. Approved records are defaults and must not be silently replaced or
silently edited. A change requires a new record that references and supersedes the
previous one; the old record is retained with updated status.

This ADR set is canonical for the decisions it covers. `docs/ARCHITECTURE.md` remains
the high-level design document and must point to these records rather than duplicate
or contradict them.

## Context

Vellum is a long-lived C project with many interacting subsystems and several
dependencies whose cost is nontrivial (text shaping, parsing, terminal emulation,
rendering). Decisions made now will be questioned months later, often by someone who
cannot reconstruct why an alternative was rejected. A decision buried in a design
document or chat history is effectively lost.

We need records that are small enough to read before touching a subsystem, explicit
about trade-offs, and versioned with the code.

## Alternatives considered

1. **Keep all decisions inside `ARCHITECTURE.md` only.** Rejected: the document grows
   large, decisions get rewritten in place, and the history and rationale disappear.
2. **Use an external wiki or issue tracker.** Rejected: decisions would drift away from
   the repository and from the code that implements them.
3. **No formal records.** Rejected: invitation to relitigate settled questions and to
   introduce dependencies by accident.
4. **Very heavy templates with extensive status workflow.** Rejected as disproportionate
   for a project of this size; the five required sections are sufficient.

## Consequences

- Every significant dependency or architectural direction gets a short record with
  verifiable evidence (version, licence, measured behaviour) where applicable.
- Rationale and rejected alternatives are preserved, which makes future reviews faster.
- There is a small ongoing obligation: update the index, keep `ARCHITECTURE.md` and
  `ROADMAP.md` synchronized, and write a superseding ADR rather than editing history.
- Anyone proposing a change must present technical evidence; preference or fashion is
  not sufficient grounds.

## Conditions that justify revisiting

- If this format proves too heavy to maintain (records consistently not written),
  simplify the template — but do not remove the records themselves.
- If the project is split into separately versioned repositories, move the records to
  the repository that owns the affected subsystem and leave pointers here.
