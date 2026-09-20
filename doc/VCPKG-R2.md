# vcpkg binary cache on R2

The build workflow can use a private Cloudflare R2 bucket through vcpkg's
experimental `x-aws` provider. Transfers go directly to the R2 S3 endpoint.
The local cache stays enabled. This branch leaves GitHub NuGet caching disabled.

## Pipeline configuration

Create a dedicated private R2 bucket using Standard storage. Configure these
GitHub Actions repository variables and secrets before enabling the integration.

| Actions setting | Kind | Pipeline environment variable | Value |
| --- | --- | --- | --- |
| `VCPKG_R2_ENABLED` | Variable | Workflow condition | `true` to enable |
| `VCPKG_R2_ENDPOINT_URL` | Variable | `R2_ENDPOINT_URL` | `https://<account-id>.r2.cloudflarestorage.com` |
| `VCPKG_R2_BUCKET` | Variable | `R2_BUCKET` | Bucket name without a path |
| `VCPKG_R2_ACCESS_KEY_ID` | Secret | `R2_ACCESS_KEY_ID` | Writer's R2 S3 access key ID |
| `VCPKG_R2_SECRET_ACCESS_KEY` | Secret | `R2_SECRET_ACCESS_KEY` | Writer's R2 S3 secret access key |
| `VCPKG_R2_READ_ACCESS_KEY_ID` | Secret | `R2_ACCESS_KEY_ID` | Reader's R2 S3 access key ID for same-repository PRs |
| `VCPKG_R2_READ_SECRET_ACCESS_KEY` | Secret | `R2_SECRET_ACCESS_KEY` | Reader's R2 S3 secret access key for same-repository PRs |

No account, bucket, or credential is embedded in the repository. Jurisdictional
`<account-id>.eu.r2.cloudflarestorage.com` and
`<account-id>.fedramp.r2.cloudflarestorage.com` endpoints are also accepted.
Objects use `cache/<ABI>.zip`. The prefix is fixed so build configuration and
retention rules address the same objects.

Use bucket-scoped Object Read & Write credentials for protected non-PR runs,
and a separate bucket-scoped Object Read credential for same-repository PRs.
The workflow sets `R2_CACHE_MODE=readwrite` for the former and `read` for the
latter. The script defaults to `read` when the mode is omitted. The read-only
credential must enforce Object Read permissions at R2; setting vcpkg to `read`
does not restrict a credential that can write.

Protect the branches and tags that should publish cache entries. A manual
dispatch on an unprotected branch skips R2. Fork PRs also skip R2 to keep the
bucket private without exposing credentials. All PRs retain local read/write
caching. For same-repository Dependabot PRs, configure the
two reader secrets under Dependabot secrets as well, using the same names.
Missing reader credentials fail setup when R2 is enabled for a same-repository
PR. Do not put R2 credentials in job-wide environment variables or expose them
to fork builds.
Anyone able to change a workflow that receives write credentials can publish
cache binaries, so limit that access to trusted maintainers.

Windows and Linux hosted runners supply AWS CLI v2. The workflow installs it
with Homebrew on macOS. Custom runners must install AWS CLI v2.13.0 or newer.
The setup script calls the repository's `vcpkg fetch aws` to find the executable
vcpkg will use, checks its version, and lists at most one object under `cache/`.
This read preflight uses `AWS_ENDPOINT_URL_S3`, without a command-line endpoint
override. A missing credential, CLI, or inaccessible bucket fails setup before
changing the job environment. It does not prove upload or restore behavior.

After preflight, the script exports R2 credentials, the S3 endpoint, region
`auto`, and the extended `VCPKG_BINARY_SOURCES` through `GITHUB_ENV`. Protected
non-PR builds can write to R2; PR builds only read it.
Packages restored from the local cache
are not automatically copied into R2. Seed required ABIs explicitly or allow
R2 to fill as dependencies rebuild.

## Retention

No lifecycle rule or Worker is deployed by this integration. Choose retention
on the bucket separately. A starting policy is expiration after 90 days under
`cache/`, with incomplete multipart uploads aborted after seven days. Age-based
expiration can delete frequently used packages. It does not measure usage or
enforce a storage quota. Retain release dependencies separately if rebuilding
them later is unacceptable.

Use a scheduled Worker only when a concrete policy needs more than native
expiration. Tracking dependencies used by successful builds also accounts for
local cache hits; remote read logs alone do not.

## Developer access

Install AWS CLI v2.13.0 or newer and obtain bucket-scoped Object Read credentials.
Set `AWS_ACCESS_KEY_ID` and `AWS_SECRET_ACCESS_KEY` through your credential
manager, or select a dedicated `AWS_PROFILE`. Then, in PowerShell:

```powershell
$env:AWS_ENDPOINT_URL_S3 = $env:R2_ENDPOINT_URL
$env:AWS_REGION = 'auto'
$env:AWS_DEFAULT_REGION = 'auto'
$env:AWS_IGNORE_CONFIGURED_ENDPOINT_URLS = 'false'
$env:AWS_PAGER = ''
$env:VCPKG_BINARY_SOURCES = "clear;default,readwrite;x-aws,s3://$($env:R2_BUCKET)/cache/,read"
cmake -S indra --preset vs2026-os
```

Set `R2_ENDPOINT_URL` and `R2_BUCKET` in the shell environment first. On macOS
or Linux, export the same values and use the appropriate CMake preset. Read-only
credentials must enforce permissions at R2; the vcpkg `read` mode alone is not
an access control. Avoid stale `AWS_SESSION_TOKEN` values when using permanent
R2 credentials.

## Live acceptance checks

Run these before relying on R2 across the build matrix. No live R2 checks were performed when this
integration was added because no target bucket or credentials were supplied.

1. On each runner platform, confirm the setup log identifies the vcpkg-selected
   AWS CLI and passes the read preflight. Check a same-repository PR reports
   `read` mode and uses the reader credential. Check a fork PR skips both R2
   setup steps, and a protected non-PR run reports `readwrite` mode.
2. Pick a real Alchemy cache ZIP larger than 100 MiB. Upload it using `aws s3 cp`
   to a unique `validation/<run-id>/` prefix, download it to a different local
   path, and compare SHA-256 hashes. Keep this separate from `cache/`. Do not
   infer integrity from multipart ETags.
3. Demonstrate a vcpkg restore with only R2 configured. Use a new build directory
   and a new install directory. For example, after a trusted producer has
   populated matching ABIs on Windows:

   ```powershell
   $run = [guid]::NewGuid().ToString('N')
   $build = Join-Path $PWD "build-r2-verify-$run"
   $env:VCPKG_BINARY_SOURCES = "clear;x-aws,s3://$($env:R2_BUCKET)/cache/,read"
   cmake -S indra --preset vs2026-os -B $build `
     -DVCPKG_TARGET_TRIPLET=x64-windows-alchemy-avx2-release `
     "-DVCPKG_INSTALLED_DIR=$build/vcpkg_installed" `
     -DVCPKG_INSTALL_OPTIONS=--only-binarycaching
   if ($LASTEXITCODE -ne 0) { throw 'Remote-only restore failed' }
   ```

   Match the producer's compiler, vcpkg revision, features, and triplets. Require
   the vcpkg log to report packages restored from AWS and a successful configure.
   `--only-binarycaching` makes a missing binary fail instead of quietly building
   from source. The fresh directories avoid Alchemy's install stamp and existing
   installed packages. `clear` removes local and NuGet cache reads.
4. With a separate empty validation prefix and another fresh build directory,
   run without `--only-binarycaching` and confirm a cache miss builds successfully.
   Use `readwrite` with the writer credential and then repeat with a fresh install
   directory and `read` to demonstrate the resulting upload and restore. Do not
   delete production cache entries to manufacture a miss.
5. With the consumer credential, confirm a known object downloads and an upload
   to a unique validation key fails with `AccessDenied`. A network failure is not
   evidence of read-only permissions. Use an authorized writer to remove only
   the validation prefix after checking it contains the intended test objects.

## Rollout and rollback

This branch already disables NuGet caching and its authentication step. R2 is
added alongside the local cache. Run the remote-only checks on each platform;
a successful build with local caching enabled does not prove that R2 served a
package. Disabling `VCPKG_R2_ENABLED` restores local-only caching immediately.
It leaves bucket contents and retention policy untouched. Cache misses build
from source.

Local setup checks:

```text
python -m unittest discover -s scripts/vcpkg -p test_*.py -v
```

## References

- [vcpkg binary caching providers](https://learn.microsoft.com/en-us/vcpkg/reference/binarycaching)
- [R2 AWS CLI setup](https://developers.cloudflare.com/r2/examples/aws/aws-cli/)
- [AWS endpoint configuration](https://docs.aws.amazon.com/cli/latest/userguide/cli-configure-endpoints.html)
- [R2 lifecycle rules](https://developers.cloudflare.com/r2/buckets/object-lifecycles/)
- [GitHub runner software inventories](https://github.com/actions/runner-images/tree/main/images)
- [GitHub Actions secret availability](https://docs.github.com/en/actions/how-tos/write-workflows/choose-what-workflows-do/use-secrets)
- [Dependabot secrets in Actions](https://docs.github.com/en/code-security/reference/supply-chain-security/troubleshoot-dependabot/dependabot-on-actions)
