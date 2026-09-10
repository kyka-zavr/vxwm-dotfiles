void
runautostart(void)
{
	const char *const *cmd = autostart;

	if (fork() == 0) {
		setsid();

		while (*cmd != NULL) {
			if (fork() == 0) {
				execl("/bin/zsh", "zsh", "-c", *cmd, NULL);
				exit(1);
			}
			cmd++;
		}
		exit(0);
	}
}
