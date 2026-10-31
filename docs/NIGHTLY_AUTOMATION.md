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

- Workflow: [nightly-verification.yml](../.github/workflows/nightly-verification.yml)
- GitHub Actions runner pricing: https://docs.github.com/en/actions/how-tos/write-workflows/choose-where-workflows-run/choose-the-runner-for-a-job
- Scheduled workflow behavior: https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#schedule
