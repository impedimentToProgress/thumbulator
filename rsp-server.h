/* rsp-server.c -- Remote Serial Protocol server for GDB

   Copyright (C) 2008 Embecosm Limited

   Contributor Jeremy Bennett <jeremy.bennett@embecosm.com>

   This file is part of Or1ksim, the OpenRISC 1000 Architectural Simulator.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 3 of the License, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   You should have received a copy of the GNU General Public License along
   with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* This program is commented throughout in a fashion suitable for processing
   with Doxygen. */


#ifndef RSP_SERVER__H
#define RSP_SERVER__H

/*! Size of the matchpoint hash table. Largest prime < 2^10 */
#define MP_HASH_SIZE  1021

/* Function prototypes for external use */
void  rsp_init ();
void  handle_rsp ();
void  rsp_exception (unsigned long int  except);
void  rsp_trap();
void rsp_check_stall();
void rsp_check_watch(unsigned int addr);

/*! Enumeration of different types of matchpoint. These have explicit values
    matching the second digit of 'z' and 'Z' packets. */
enum mp_type {
  BP_MEMORY   = 0,
  BP_HARDWARE = 1,
  WP_WRITE    = 2,
  WP_READ     = 3,
  WP_ACCESS   = 4
};

/*! Data structure for a matchpoint hash table entry */
struct mp_entry
{
  enum mp_type       type;		/*!< Type of matchpoint */
  unsigned long int  addr;		/*!< Address with the matchpoint */
  unsigned long int  instr;		/*!< Substituted instruction */
  struct mp_entry   *next;		/*!< Next entry with this hash */
};

/*! Central data for the RSP connection */
struct RSP 
{
  int                client_waiting;	/*!< Is client waiting a response? */
  int                proto_num;		/*!< Number of the protocol used */
  int                client_fd;		/*!< FD for talking to GDB */
  int                sigval;		/*!< GDB signal for any exception */
  unsigned long int  start_addr;	/*!< Start of last run */
  struct mp_entry   *mp_hash[MP_HASH_SIZE];	/*!< Matchpoint hash table */
  unsigned int       port;
  int                stalled;
  int                stepping;
};

extern struct RSP rsp;

#endif	/* RSP_SERVER__H */
