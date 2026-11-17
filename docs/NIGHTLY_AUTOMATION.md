# Nightly automation

## Purpose

This repository uses a deterministic, no-LLM nightly verification workflow. It does not call an AI API and does not make autonomous source-code edits.

## Schedule and workflow location

- Workflow: `.github/workflows/nightly-verification.yml`
- The workflow definition is kept on the repository default branch (`main`) because GitHub scheduled events run workflows from the default branch.
- The workflow checks out and tests `ios/initial-support`.
- Schedule: 18:17 UTC daily (03:17 Japan Standard Time). GitHub may delay scheduled runs during periods of high load, so this is not an exact start-time guarantee.
- Manual execution is available through the workflow's `workflow_dispatch` trigger.

## Checks performed

1. Configure the CMake project on a Windows GitHub-hosted runner.
2. Build the Release configuration.
3. Run the application smoke test.
4. Run CTest and report failures in the Actions job log.
5. Write a short run summary to the GitHub Actions run summary.

## Cost and AI limitations

- The repository is public. GitHub documents standard GitHub-hosted runner usage as free and unlimited for public repositories. This workflow uses the standard `windows-latest` runner, not a paid larger runner.
- The workflow does not use OpenAI API, any paid model endpoint, or other metered AI service.
- This is automated verification, not autonomous AI development. It can detect build/test failures, but it cannot independently reason about feature requests, invent fixes, or update source documentation based on semantic code review.
- The existing build workflow remains responsible for the Windows and iOS build pipeline on branch pushes. The nightly workflow currently repeats the Windows build, smoke test, and unit tests.

## Safety rules

- Do not add API keys or paid-service secrets to enable this workflow.
- Do not auto-commit generated changes or modify engine source files from this workflow.
- Treat CI success as build/test evidence only; it does not prove rendering correctness on a physical iPhone.
- Review failures and make source changes through the normal `ios/initial-support` development process.

## References

- Workflow on `main`: https://github.com/shton13275-2/GasolineEngine/blob/main/.github/workflows/nightly-verification.yml
- GitHub Actions runner pricing: https://docs.github.com/en/actions/how-tos/write-workflows/choose-where-workflows-run/choose-the-runner-for-a-job
- Scheduled workflow behavior: https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#schedule


## Observed scheduled-run failure — 2026-11-16

- Scheduled Nightly verification runs [#13](https://github.com/shton13275-2/GasolineEngine/actions/runs/43527643526), [#14](https://github.com/shton13275-2/GasolineEngine/actions/runs/43647712486), and [#15](https://github.com/shton13275-2/GasolineEngine/actions/runs/43771220392) all concluded `failure`.
- In all three runs, Configure, Build, Smoke test, and Run unit tests succeeded; CTest reported 13/13 passed. Only the final `Write audit summary` step failed.
- Run #15's generated PowerShell script failed to parse at line 5 with `You must provide a value expression following the '-' operator.` The workflow writes Markdown inline-code backticks inside double-quoted PowerShell strings. PowerShell uses backtick as its escape character, so the summary script is syntactically invalid. The engine build and tests passed, but the workflow as a whole did not.
- The scheduled workflow definition is stored on `main`, the repository default branch, because GitHub scheduled events use the default-branch workflow file. This development run is restricted to `ios/initial-support` and does not modify `main`; therefore the defect is recorded here but not repaired. A change made only to a workflow copy on `ios/initial-support` would not fix scheduled runs.
- When a separate change to `main` is authorized, compute the source commit in a PowerShell variable and use single-quoted strings for static Markdown lines (or otherwise avoid unescaped Markdown backticks), then run a manual dispatch and confirm the audit summary completes.


## Status recheck — 2026-11-17

- The latest main-branch workflow-run listing returned Nightly verification run [#15](https://github.com/shton13275-2/GasolineEngine/actions/runs/43771220392) as the newest Nightly run; no later Nightly run was present in that returned list.
- Run #15 remains `failure`. Its job steps show Configure, Build, Smoke test, and Run unit tests succeeded, CTest reported 13/13 passed, and only `Write audit summary` failed. The generated PowerShell script reports a parser error at line 5: `You must provide a value expression following the '-' operator.`
- The workflow remains on the repository default branch `main`. This development review did not modify `main`; a fix confined to `ios/initial-support` would not change the scheduled workflow definition.
