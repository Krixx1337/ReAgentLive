# Installing or updating ReAgentLive

For a user asking to install, update or upgrade ReAgentLive itself. The
procedures live in `docs/agents/`; this file picks one and reads its
newest text.

1. Find the clone: the repo holding this skill folder
   (`git -C <skill folder> rev-parse --show-toplevel`, through its link),
   else the setup record's `dir`. Neither: a first install; follow
   `docs/agents/setup.md` from `main` through SKILL.md's resolver.
2. Fetch before reading, so steps added upstream apply:
   `git -C <clone> fetch origin`, then
   `git -C <clone> show origin/main:docs/agents/update.md`. The fetch fails
   (offline, no remote): read the local `docs/agents/update.md` and say so.
3. Follow it from step 1; it sends a submodule host to its own section.
   The host project's notes (its build, its commit rules) win over the
   generic steps.
4. After the update, this skill may have changed: reread SKILL.md before
   the next investigation.
