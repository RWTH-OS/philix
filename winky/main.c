#include "cga.h"
#include "xeon_phi.h"
#include "winky.h"

static void
wait (int n)
{
	while (n--) {
		udelay(1000000);
	}
}


/*
 * a stupidly simple Xeon Phi Kernel for 
 * philix demonstration purposes. The CGA code comes
 * straight from osdev.org
 *
 */
void
main (unsigned long mbd, unsigned long magic)
{
	term_init();

	phi_init();

	phi_notify_boot_ok();

	term_print("Welcome to the philix example OS, Winky the Cat!\n\n");

	wait(3);

	splash();

	wait(10);

	/* shut it down */
	phi_send_cons_shutdown();
}
