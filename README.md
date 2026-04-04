cellc is a container runtime from scratch in C . Right now the file descriptions 

- main.c — only responsible for parsing CLI arguments and dispatching to the right command. When I eventually have run, ps, exec, kill — main just figures out which one was called and hands off.

- container.c — the orchestrator. It holds the container_run() function which is the top level function that sets everything up in the right order — calls namespace setup, filesystem setup, cgroup setup, then finally clone(). Think of it as the director.

- namespace.c — everything related to clone() flags, the parent-child synchronization pipe, and the child's namespace setup function that runs after clone().

- utils.c — error handling helpers, logging, small reusable things. Build a good die(const char *fmt, ...) function early that prints an error and exits cleanly

- fs.c - it sets up overlayfs for basic copy on write for filesystems and also mounts separate filesystem for each container. This is done by pivoting from host fs to an independent Alpine Linux based fs for lightweight fs.



Whats Done : 

- A process running in isolated PID, Mount, UTS, IPC, and Network namespaces
- Custom hostname set inside UTS namespace without touching the host
- PID 1 inside the container confirmed
- Parent-child synchronization pipe working
- Installed Alpine Linux FS , but only on the read only base layer
- container has its own isolated filesystem (Alpine, not the host) and its own hostname
- Copy-on-write semantics via OverlayFS — writes go to upper layer, base Alpine untouched
- cgroups v2 — resource limits. CPU, memory, and PID limits enforced by the kernel.
- configure veth and network namespaces using netns. 

Whats To Be Done: 
- run CLI to set up docker like features like image pull , ps , kill and so on. 
