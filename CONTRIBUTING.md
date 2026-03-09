# Contributor guidelines

Thanks for your interest in contributing to Alchemy Viewer! This document
summarizes some of the most important points for people looking to contribute
to the project especially those looking to provide bug reports and code
changes.

## Table of contents

- [Contributor guidelines](#contributor-guidelines)
  - [Table of contents](#table-of-contents)
  - [Communication](#communication)
  - [Reporting Bugs and Requesting Features](#reporting-bugs-and-requesting-features)
    - [Importance of Your Feedback](#importance-of-your-feedback)
    - [Our Relationship with Linden Lab and Code Inheritance](#our-relationship-with-linden-lab-and-code-inheritance)
    - [Reporting Bugs to Linden Lab](#reporting-bugs-to-linden-lab)
    - [Submitting Feedback to Alchemy Viewer](#submitting-feedback-to-alchemy-viewer)
  - [Contributing pull requests](#contributing-pull-requests)

## Communication

Alchemy Viewer has multiple channels for communication. Some of these channels are
more end-user focused while others are more tailored for developer-to-developer or support.

- [Our Discord][discord] is the primary community engagement platform for the project.
  This is where we announce our releases, answer questions from the community,
  and provide support to users. This is also a great place for developers to interact
  with the Alchemy users to determine if their feature is interesting to them.
- [Github issues][] provide a means for developers and contributors to organize
  their work and collaborate with other developers. By default most user-facing
  discussions should happen on [the Discord][discord] so that they are
  visible to more people, and can build consensus.

## Reporting Bugs and Requesting Features

### Importance of Your Feedback

We value your feedback and invite you to help us identify bugs and suggest new features for Alchemy Viewer.
Your input is crucial in shaping the future of our project.

Your cooperation with these guidelines will help us improve Alchemy Viewer,
and ensures that Linden Lab can address broader issues within their codebase.

### Our Relationship with Linden Lab and Code Inheritance

It's important to note that while we are not affiliated with or endorsed by Linden Lab,
the creators of Second Life, our viewer incorporates a significant amount of code from them.

This shared codebase means some issues may originate from the Linden Lab viewer,
and addressing these effectively benefits both viewers.

### Reporting Bugs to Linden Lab

If you encounter defects or unwanted behavior that are inherited from the official Second Life viewer,
please report these issues directly to Linden Lab.

Before submitting a bug report to Alchemy Viewer, kindly check the [official Second Life viewer][lindenviewer]
to determine if the defect exists there as well.

Reporting it to Linden Lab, if it hasn't already been reported, will ensure they are aware of the issue.
If you are unsure, you are encouraged to share your findings with an Alchemy team member to receive guidance.
Linden Lab's page for submitting bug reports can be found on [feedback.secondlife.com/bug-reports][lindenbugs].

### Submitting Feedback to Alchemy Viewer

If the defect is unique to Alchemy Viewer or pertains to features exclusive to our viewer, please proceed to submit your bug report to us on Github. Provide as much detail as possible, including steps to reproduce the issue, logs, and screenshots if available. This information will help us diagnose and address the problem more efficiently.

## Contributing pull requests

If you wish to contribute a new pull request, please ensure that:

- You talk to other developers about how best to implement the work.
- The functionality is desired. Be sure to talk to users and members of the Alchemy
  team to ensure the work is a good idea and will be accepted.
- The work is high quality and the PR follows [PR etiquette][]
- You have tested the work locally

The [Git Style Guide](https://github.com/agis/git-style-guide) is also a good
reference for best git practices.

[PR etiquette]: https://gist.github.com/mikepea/863f63d6e37281e329f8
[Github issues]: https://github.com/AlchemyViewer/Alchemy/issues
[discord]: https://discordapp.com/invite/KugCgs6
[lindenviewer]: https://releasenotes.secondlife.com/viewer.html
[lindenbugs]: https://feedback.secondlife.com/bug-reports
