# Mnemosyne

Mnemosyne is simplified version control system. Track changes, commit versions, revert to previous states, and send changes to remotes.

## Features

- Local version vontrol: track files, commit changes, and revert. Done.
- Remote support: add remotes, send commits, fetch changes, or even create a remote repository.
- Human-friendly: simple commands, meaningful messages, no detached HEAD states.

## Compile and Install

### Compile Directly

To compile Mnemosyne manually, use:

		cc mnemos.c -o mnemos

### Install via Makefile

For an easier installation process, use the provided Makefile:

Build the binary:

		make

Install the binary to /usr/local/bin for global access:

		sudo make install

If you want to uninstall:

		sudo make uninstall

## Usage

Mnemosyne core uses shorthand mnemos commands.

### Commands

#### Initialization

Initialize a new Mnemosyne repository in the current directory:

	    mnemos init

This creates the .mnemos directory to store commits, objects, and the index.

#### Tracking Files

Track specific file:

	    mnemos track <file>

Track all files in the current directory:

	    mnemos track -a

#### Committing Changes

Commit tracked changes with a message:

		mnemos commit <message>

#### Reverting Changes

To find available commit hashes, simply list them with:

	    ls .mnemos/commits

**Why this approach? Because we want to treat everything like a file, when UNIX allows. Your commits are not buried in database, they are real directories stored in .mnemos/commits, directly accessible with standard tools like ls.**

**This means *ls* gives you all the context you need. Each commit hash is its own entity, to revert, inspect, or send to remotes without blood moon ceremonies.**

Revert the repository to a specific commit using its hash:

	    mnemos revert <commit_hash>

#### Remote Support

Set a remote repository path:

		mnemos remote <path>

Mnemosyne remotes work with rsync over ssh. No HTTP, we're not building a web service. When improved it will use 9P(as the gods intended), but for now, rsync over ssh gets the job done.

Send commits to configured remote:

		mnemos send

Fetch commits from configured remote:

		mnemos fetch

Create remote repository from your local Mnemosyne repository:

		mnemos create-remote <remote_path>

## Deployment

To automatically trigger a deploy script on your server when you *mnemos send*, you can utilize SSH and a simple server-side hook mechanism, much like Git's post-receive hooks but fully customizable with UNIX tools.

### Add a deploy hook on the server

On your (assuming UNIX or UNIX-like) server, create a script that will handle deployment whenever Mnemosyne sends changes:

		sudo vi /usr/local/bin/mnemos-deploy.sh

*mnemos-deploy.sh*

		#!/bin/sh
		# Mnemos deploy script for Svelte TS project

		REPO_DIR="/path/to/mnemos-repo"
		DEPLOY_DIR="/var/www/project"
		BUILD_CMD="vite build"

		echo "==== Mnemos Deploy Script ===="

		# Ensure we're in the repository directory
		cd "$REPO_DIR" || exit 1

		# Sync files from repo to deploy directory
		rsync -av --exclude=".mnemos" "$REPO_DIR/" "$DEPLOY_DIR/"

		# Build the project (if needed)
		echo "Building project..."
		cd "$DEPLOY_DIR" || exit 1
		$BUILD_CMD

		# Print success message
		echo "Deployment completed successfully!"

Make it executable: 

		chmod +x /usr/local/bin/mnemos-deploy.sh

### Trigger deploy script dfter mnemos send

Since Mnemosyne uses rsync to send commits, you can detect new changes on the server using a file-based trigger.

Set up post-receive trigger - create a file in your repository to act as trigger for the deploy script:

		vi /path/to/mnemos-repo/.mnemos/post-receive

*.mnemos/post-receive*

		#!/bin/sh
		# Trigger deploy script after mnemos sync
		/usr/local/bin/mnemos-deploy.sh

Make it executable:

		chmod +x /path/to/mnemos-repo/.mnemos/post-receive

Now, configure your server to run the trigger script after any rsync-based mnemos send.

FreeBSD uses kqueue (ie. sysutils/watchexec), Linux uses inotify. FreeBSD example:

		pkg install watchexec

		watchexec --restart -- /path/to/mnemos-repo/.mnemos/post-receive

Whenever a change occurs (after mnemos send), this will trigger the deploy script.

Option 2: Use SSH Command Chaining

Another approach is to directly run the deploy script after mnemos send.

		mnemos send && ssh user@server "/usr/local/bin/mnemos-deploy.sh"

## License

MIT

This project is open source. Use it, modify it, share it.
