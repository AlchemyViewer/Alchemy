#!/usr/bin/env python3
"""Turn the macOS signing certificate and notarization key into the build's secrets.

The hosted build (.github/workflows/build.yaml) signs with a Developer ID
Application certificate and notarizes with an App Store Connect API key,
read from these repository secrets:

  MACOS_CERTIFICATE           the certificate and its private key, a .p12, in base64
  MACOS_CERTIFICATE_PASSWORD  the .p12's password
  MACOS_NOTARY_KEY            the API key, the .p8's text
  MACOS_NOTARY_KEY_ID         the API key's ID
  MACOS_NOTARY_ISSUER_ID      the issuer ID of a team key; unset for an individual key

Each is checked as the build will use it. The .p12 must hold one private
key and one Developer ID Application identity that macOS trusts, imported
into a keychain of the script's own as the build imports it; it is stored
encrypted again under a password the script makes, in the format the
build's `security import` reads. The API key is tried against the notary
service unless --no-verify says not to. Then the secrets are stored with
`gh secret set`, or written to files, a file a secret, to paste into the
repository's settings.

Nothing secret is printed or put on a command line: passwords reach
openssl through a pipe, and gh takes each value on its standard input.
What the script writes along the way stays in a private directory it
removes.

The .p12: in Keychain Access, under My Certificates, select the
"Developer ID Application: <name> (<team>)" certificate, which carries its
private key, and use File > Export Items to save it as a .p12 with a
password. The API key: in App Store Connect, under Users and Access >
Integrations, a team key with the Developer role, or an individual key.
Its .p8 downloads once, as AuthKey_<key ID>.p8; a team key's issuer ID
heads the page.

  scripts/signing/macos_signing_secrets.py --certificate DeveloperID.p12 \\
      --notary-key AuthKey_ABCDE12345.p8 --issuer-id <uuid> --repo AlchemyViewer/Alchemy
"""

import argparse
import base64
import datetime
import getpass
import os
from pathlib import Path
import re
import secrets
import subprocess
import sys
import tempfile
import uuid

# The system's LibreSSL: what it writes, macOS's security tool reads.
OPENSSL = "/usr/bin/openssl"
EXPIRY_WARNING_DAYS = 30


class Error(Exception):
    pass


def run(command, **kwargs):
    kwargs.setdefault("stdout", subprocess.PIPE)
    kwargs.setdefault("stderr", subprocess.PIPE)
    return subprocess.run([str(part) for part in command], check=False, **kwargs)


def write_private(path, data):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(data)


def openssl(arguments, stdin=None, **pipes):
    """Runs openssl, feeding it each of `pipes` through a pipe of its own.

    In the arguments, @fd:NAME@ becomes fd:N, for -passin and -passout, and
    @path:NAME@ becomes /dev/fd/N, for a file.
    """
    fds = {}
    try:
        for name, data in pipes.items():
            read_end, write_end = os.pipe()
            fds[name] = read_end
            # Small enough to sit in the pipe until openssl reads it.
            os.write(write_end, data)
            os.close(write_end)
        command = [OPENSSL]
        for argument in map(str, arguments):
            for name, fd in fds.items():
                argument = argument.replace(f"@fd:{name}@", f"fd:{fd}").replace(f"@path:{name}@", f"/dev/fd/{fd}")
            command.append(argument)
        return run(command, input=stdin, pass_fds=tuple(fds.values()))
    finally:
        for fd in fds.values():
            os.close(fd)


def search_list():
    result = run(["security", "list-keychains", "-d", "user"], text=True)
    return [line.strip().strip('"') for line in result.stdout.splitlines() if line.strip()]


def identities(keychain, *options):
    """The identities find-identity lists, by hash, as (name, problem)."""
    result = run(["security", "find-identity", *options, keychain], text=True)
    found = {}
    for sha1, name, problem in re.findall(
        r'^\s*\d+\)\s+([0-9A-F]{40})\s+"([^"]*)"(?:\s+\(([^)]*)\))?\s*$', result.stdout, re.MULTILINE
    ):
        found.setdefault(sha1, (name, problem))
    return found


def certificate_expiry(keychain, sha1):
    result = run(["security", "find-certificate", "-a", "-Z", "-p", keychain], text=True)
    match = re.search(
        rf"SHA-1 hash: {sha1}\n(-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----)",
        result.stdout, re.DOTALL,
    )
    if not match:
        raise Error("could not find the identity's certificate")
    result = run([OPENSSL, "x509", "-noout", "-enddate"], input=match.group(1), text=True)
    end = result.stdout.strip().removeprefix("notAfter=")
    return datetime.datetime.strptime(end, "%b %d %H:%M:%S %Y %Z").replace(tzinfo=datetime.timezone.utc)


def check_identity(p12, password, workdir):
    """Imports a .p12 into a keychain of its own, as the build does, and checks what it holds."""
    keychain = workdir / "check.keychain-db"
    keychain_password = secrets.token_urlsafe(24)
    before = search_list()
    try:
        result = run(["security", "create-keychain", "-p", keychain_password, keychain])
        if result.returncode:
            raise Error(f"could not create a keychain to check the certificate in: {result.stderr.decode().strip()}")
        result = run(["security", "import", p12, "-k", keychain, "-f", "pkcs12", "-P", password])
        if result.returncode:
            raise Error(f"macOS's security tool cannot import the certificate: {result.stderr.decode().strip()}")

        dump = run(["security", "dump-keychain", keychain], text=True).stdout
        keys = len(re.findall(r"^class: 0x00000010\b", dump, re.MULTILINE))
        if keys != 1:
            raise Error(f"the .p12 holds {keys} private keys; export only the Developer ID Application certificate")

        found = identities(keychain)
        if len(found) != 1:
            raise Error(f"the .p12 holds {len(found)} identities; export only the Developer ID Application certificate")
        sha1, (name, _) = next(iter(found.items()))
        if not name.startswith("Developer ID Application: "):
            raise Error(f'"{name}" is not a Developer ID Application certificate')

        if sha1 not in identities(keychain, "-v", "-p", "codesigning"):
            _, problem = identities(keychain, "-p", "codesigning").get(sha1, (name, ""))
            raise Error(f'macOS does not accept "{name}" for code signing ({problem or "no reason given"})')

        expiry = certificate_expiry(keychain, sha1)
        return name, expiry
    finally:
        run(["security", "delete-keychain", keychain])
        if search_list() != before:
            run(["security", "list-keychains", "-d", "user", "-s", *before])


def prepare_certificate(path, workdir):
    password = getpass.getpass(f"Password for {path.name}: ")

    # The key and certificates, unencrypted, through memory only.
    result = openssl(
        ["pkcs12", "-in", path, "-passin", "@fd:password@", "-nodes"], password=password.encode() + b"\n"
    )
    if result.returncode:
        raise Error(f"could not open {path}: wrong password, or not a .p12")
    keys = re.findall(
        rb"-----BEGIN [A-Z ]*PRIVATE KEY-----.*?-----END [A-Z ]*PRIVATE KEY-----\n", result.stdout, re.DOTALL
    )
    certificates = b"".join(
        re.findall(rb"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----\n", result.stdout, re.DOTALL)
    )
    if len(keys) != 1:
        raise Error(f"the .p12 holds {len(keys)} private keys; export only the Developer ID Application certificate")

    # Encrypted again under a password made here, and, to check it with the
    # security tool without putting that password on a command line, a twin
    # made the same way under a throwaway one. openssl reads the key and the
    # certificates in two passes, so the key comes through a pipe of its own.
    def export(new_password):
        result = openssl(
            ["pkcs12", "-export", "-inkey", "@path:key@", "-passout", "@fd:password@"],
            stdin=certificates, key=keys[0], password=new_password.encode() + b"\n",
        )
        if result.returncode:
            raise Error(f"could not encrypt the certificate again: {result.stderr.decode().strip()}")
        return result.stdout

    new_password = secrets.token_urlsafe(32)
    p12 = export(new_password)
    throwaway = secrets.token_urlsafe(24)
    twin = workdir / "twin.p12"
    write_private(twin, export(throwaway))
    name, expiry = check_identity(twin, throwaway, workdir)

    # And the one stored opens with its password.
    result = openssl(
        ["pkcs12", "-noout", "-passin", "@fd:password@"], stdin=p12, password=new_password.encode() + b"\n"
    )
    if result.returncode:
        raise Error("the certificate encrypted again does not open with its password")

    days = (expiry - datetime.datetime.now(datetime.timezone.utc)).days
    print(f"Certificate: {name}, expires {expiry:%Y-%m-%d}", flush=True)
    if days < EXPIRY_WARNING_DAYS:
        print(f"warning: the certificate expires in {days} days", file=sys.stderr)
    return {
        "MACOS_CERTIFICATE": base64.b64encode(p12).decode(),
        "MACOS_CERTIFICATE_PASSWORD": new_password,
    }


def prepare_notary_key(path, key_id, issuer_id, verify):
    if not key_id:
        match = re.fullmatch(r"AuthKey_([A-Z0-9]+)\.p8", path.name)
        if not match:
            raise Error("pass --key-id, or keep the .p8's name, AuthKey_<key ID>.p8")
        key_id = match.group(1)
    if not re.fullmatch(r"[A-Z0-9]{10,}", key_id):
        raise Error(f"{key_id} is not an App Store Connect key ID")
    if issuer_id:
        try:
            issuer_id = str(uuid.UUID(issuer_id))
        except ValueError:
            raise Error(f"{issuer_id} is not an issuer ID, which is a UUID") from None

    text = path.read_text().replace("\r\n", "\n").strip()
    if not re.fullmatch(r"-----BEGIN PRIVATE KEY-----\n[A-Za-z0-9+/=\n]+\n-----END PRIVATE KEY-----", text) or run(
        [OPENSSL, "pkey", "-noout"], input=text.encode()
    ).returncode:
        raise Error(f"{path} is not an App Store Connect API key")

    if verify:
        command = ["xcrun", "notarytool", "history", "--key", path, "--key-id", key_id, "--output-format", "json"]
        if issuer_id:
            command += ["--issuer", issuer_id]
        result = run(command, text=True, timeout=120)
        if result.returncode:
            kind = "a team key with that issuer" if issuer_id else "an individual key (a team key needs --issuer-id)"
            raise Error(f"the notary service refused the key as {kind}: {result.stderr.strip()}")
        print(f"Notary key: {key_id}, accepted by the notary service")
    else:
        print(f"Notary key: {key_id}, not tried against the notary service")

    values = {"MACOS_NOTARY_KEY": text, "MACOS_NOTARY_KEY_ID": key_id}
    if issuer_id:
        values["MACOS_NOTARY_ISSUER_ID"] = issuer_id
    return values


def store_in_github(repo, values, remove, assume_yes):
    print(f"\nIn {repo}:")
    for name in values:
        print(f"  set     {name}")
    for name in remove:
        print(f"  remove  {name}")
    if not assume_yes and input("Go ahead? [y/N] ").strip().lower() not in ("y", "yes"):
        raise Error("nothing stored")

    for name, value in values.items():
        result = run(["gh", "secret", "set", name, "--repo", repo], input=value.encode())
        if result.returncode:
            raise Error(
                f"gh could not set {name}, though any listed above it are set: {result.stderr.decode().strip()}"
            )
    if remove:
        result = run(["gh", "secret", "list", "--repo", repo, "--json", "name", "--jq", ".[].name"], text=True)
        if result.returncode:
            raise Error(f"gh could not list the secrets: {result.stderr.strip()}")
        for name in set(remove) & set(result.stdout.split()):
            result = run(["gh", "secret", "delete", name, "--repo", repo])
            if result.returncode:
                raise Error(f"gh could not remove {name}: {result.stderr.decode().strip()}")
    print("Stored.")


def store_in_files(directory, values, remove):
    directory.mkdir(mode=0o700)
    for name, value in values.items():
        write_private(directory / name, value.encode())
    print(f"\nWrote {', '.join(values)} to {directory}, readable by you alone.")
    for name in remove:
        print(f"Leave {name} unset: an individual key has no issuer.")
    print("Paste each into the repository's Actions secrets, then delete the directory.")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        epilog="See the top of this script for what each secret is and where to get it.",
    )
    parser.add_argument("--certificate", type=Path, help="the Developer ID Application .p12")
    parser.add_argument("--notary-key", type=Path, help="the App Store Connect API key, AuthKey_<key ID>.p8")
    parser.add_argument("--key-id", help="the API key's ID, when the .p8's name does not carry it")
    parser.add_argument("--issuer-id", help="the issuer ID, for a team key; leave it out for an individual key")
    parser.add_argument("--no-verify", action="store_true", help="do not try the API key against the notary service")
    where = parser.add_mutually_exclusive_group(required=True)
    where.add_argument("--repo", help="store the secrets in this repository, OWNER/NAME, with gh")
    where.add_argument("--output", type=Path, help="write the secrets to files in this new directory")
    parser.add_argument("--yes", action="store_true", help="store in the repository without asking")
    args = parser.parse_args()

    if sys.platform != "darwin":
        parser.error("this needs macOS's security tool")
    if not args.certificate and not args.notary_key:
        parser.error("give --certificate, --notary-key, or both")
    if args.notary_key is None and (args.key_id or args.issuer_id):
        parser.error("--key-id and --issuer-id go with --notary-key")
    if args.output and args.output.exists():
        parser.error(f"{args.output} exists; name a new directory")

    try:
        values = {}
        remove = []
        with tempfile.TemporaryDirectory(prefix="macos-signing-") as workdir:
            if args.certificate:
                values.update(prepare_certificate(args.certificate, Path(workdir)))
        if args.notary_key:
            values.update(prepare_notary_key(args.notary_key, args.key_id, args.issuer_id, not args.no_verify))
            if not args.issuer_id:
                remove.append("MACOS_NOTARY_ISSUER_ID")

        if args.repo:
            store_in_github(args.repo, values, remove, args.yes)
        else:
            store_in_files(args.output, values, remove)
    except (Error, OSError, subprocess.TimeoutExpired) as error:
        sys.exit(f"error: {error}")
    except KeyboardInterrupt:
        sys.exit("interrupted")


if __name__ == "__main__":
    main()
