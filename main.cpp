/******************************************************************************

******************************************************************************/

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <errno.h>
#include <wiringPiSPI.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

using namespace std;

bool debug_flag = false;

// SPI Speed as per the datasheet of the accelerometer : 1,2 MHz
#define SPI_SPEED 1200000

// Minimum square value for triggering an output
#define MIN_TRIGGER 0.4 // in G

// Coefficient for 10bit ADC --> G
#define G_COEF (3300/1024)/800

// Offsets are calculated here for faaaaastness (heuristics)
#define X_OFFSET -512 - 2
#define Y_OFFSET -512 + 67
#define Z_OFFSET -512 - 248 + 41 // Account for gravity (1G = 800mV at 1.5 precision)  // 800mV/(3,3V/1023)

// Time
#define ONE_OVER_CPS (1000000/CLOCKS_PER_SEC)

// Sampling rate for the ADC
unsigned int sampling_rate_in_us = 1000000; // 1 second
// Type of accelerometer
char * type;

// We declare globals for faster reads
unsigned int channel_select;
unsigned char buffer[2];

// channel = 0, 1, 2
unsigned int readADC(int channel) {

   if (channel == 0) {
      buffer[0] = 0x60;
      buffer[1] = 0x00;
      channel_select = 0;
   } else if (channel == 1) {
      buffer[0] = 0x70;
      buffer[1] = 0x00;
      channel_select = 0;
   } else {
      buffer[0] = 0x60;
      buffer[1] = 0x00;
      channel_select = 1;
   }

   // We write two bits
   /*
      Note that the SPI protocol "returns" the same number of bits as sent,
      and in this case the 10-bit ADC data is encoded in the last two bits
      of the first byte and the eight bits of the second byte. I chose to
      retrieve the data in MSB-first order, so the result is the sum of
      (the bottom two bits of the first byte shifted up by eight bits) + the second byte.
   */
   wiringPiSPIDataRW(channel_select, buffer, 2);

   // Used just for testing :
   // unsigned int msb = buffer[1] + ((buffer[0] & 0x03) << 8);
   // unsigned int lsb = buffer[0] + ((buffer[1] & 0x03) << 8);

   return buffer[1] + ((buffer[0] & 0x03) << 8);
}

int main(int argc, char *argv[])
{
   // File descriptors for SPI
   int fd_mpc3002_1, fd_mpc3002_2;

   // Data values
   int x, y, z;
   double xg, yg, zg;

   // Running an average on the whole sim
   long x_avg, y_avg, z_avg, count_avg;
   x_avg = 0; y_avg = 0; z_avg = 0; count_avg = 0;

   // For clocking and precision
   clock_t tic, toc;
   unsigned int elapsed_time_in_us;

   if (argc == 1) {
      printf("Caribe Wave Sensor Pusher — pushes sensor data to stdout for next stage.\n");
      printf("Usage :\n");
      printf("  ./main [sampling rate] [debug]\n");
      printf("\n");
      printf("Arguments :\n");
      printf("  - type: type of accelerometer : ADXL, LIS or MMA (string).\n");
      printf("  - sampling rate: in milliseconds, between 0 and 1000.\n");
      printf("  - debug: 1 or 0.\n");
      printf("\n");
      exit(0);
   } else if (argc == 2) {
      type = argv[1];
   } else if (argc == 3) {
      type = argv[1];
      sampling_rate_in_us = max(0, min(1000, atoi(argv[2]))) * 1000;
   } else if (argc == 4) {
      type = argv[1];
      sampling_rate_in_us = max(0, min(1000, atoi(argv[2]))) * 1000;
      debug_flag = argv[3][0] == '1' ? true : false;
      if (debug_flag) printf("Debug flag is TRUE.\n");
   } else {
      if (debug_flag) printf("Too many arguments supplied. Default sampling rate used.\n");
   }

   if (debug_flag) printf("Starting up.\n");
   if (debug_flag) printf("Sampling rate will be %d µs.\n", sampling_rate_in_us);
   if (debug_flag) printf("Accel. type is : %s.\n", type);
   if (debug_flag) printf("Initializing sensor pusher ...\n");

   // Configure the interface.
   fd_mpc3002_1 = wiringPiSPISetup(0, SPI_SPEED);
   fd_mpc3002_2 = wiringPiSPISetup(1, SPI_SPEED);

   if (debug_flag) printf("SPI setup : OK.\n");

   if (debug_flag) printf("Initing first MPC3002  : %d.\n", fd_mpc3002_1);
   if (debug_flag) printf("Initing second MPC3002 : %d.\n", fd_mpc3002_2);

   if (debug_flag) printf("\n");

   do {

      tic = clock();

      x = 0; y = 0; z = 0;
      for (int i = 0; i < 10; ++i)
      {
         x += readADC(0) + X_OFFSET;
         y += readADC(1) + Y_OFFSET;
         z += readADC(2) + Z_OFFSET;
      }

      // Average on 1ms approx.
      x = x/10;
      y = y/10;
      z = z/10;

      xg = (double) x*G_COEF;
      yg = (double) y*G_COEF;
      zg = (double) z*G_COEF;

      // 1G on at least one axis triggers the output
      if ( xg*xg + yg*yg + zg*zg > MIN_TRIGGER || debug_flag) {
         // Format : X_10bit X_G Y_10bit Y_G Z_1Obit Z_G
         printf("%d %f %d %f %d %f\n", x, xg, y, yg, z, zg);

         if (debug_flag) {
            x_avg += x; y_avg += y; z_avg += z; 
            count_avg++;
            printf("  [Average since launch %ld %ld %ld]\n", x_avg/count_avg, y_avg/count_avg, z_avg/count_avg);
         }
      }

      toc = clock();
      elapsed_time_in_us = (toc - tic) * ONE_OVER_CPS; // in microsecs
      if (debug_flag) printf("  [Sampling took %d us]\n", elapsed_time_in_us);

      usleep(sampling_rate_in_us - max(0, static_cast<int>(min(elapsed_time_in_us, sampling_rate_in_us))));

   } while(true);

   if (debug_flag) printf("Exiting");

}
