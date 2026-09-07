# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| 1.0.x | yes |

## Reporting a vulnerability

Please do not report security issues through public issues, pull requests or discussions.

Use GitHub's private reporting instead: on the repository page open **Security**, then **Report a vulnerability**. The report stays private until a fix is available.

Please include:

- the version, as reported by `angscheidrdiffer --version`
- the operating system and the Qt version
- what you observed and how to reproduce it
- the effect you consider possible

## What to expect

This is a project maintained in spare time. There is no guaranteed response time and no service level agreement. Reports are read and answered as time allows.

## Scope

The application runs locally. It reads and writes only the files named on the command line or opened through the interface, and it opens no network connection.

In scope are defects in the application and in the delivered scripts, in particular:

- reading or writing a path that was not requested
- data loss on saving or merging, for example content that is silently dropped
- a crash or a memory error triggered by the content of a compared file

Out of scope are:

- vulnerabilities in Qt or in other third-party libraries, unless the application uses them in a way that creates the issue. Report those to the project concerned
- the consequences of comparing files the user has no permission to read, which the operating system decides
