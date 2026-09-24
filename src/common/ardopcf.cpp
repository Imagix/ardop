/*
 * ardopcf main function
 *
 * Invokes the platform-specific main function from either
 * ALSASound.c (Linux) or Waveout.c (Windows)
 */
#include "common/platformapi.hpp"

int main(int argc, char *argv[])
{
	return platform_main(argc, argv);
}
