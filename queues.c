/*
 * abc2midi - program to convert abc files to MIDI files.
 * Copyright (C) 1999 James Allwright
 * e-mail: J.R.Allwright@westminster.ac.uk
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 *
 */

/* queues.c
 * This file is part of the code for abc2midi.
 * Notes due to finish in the future are held in a queue (linked list)
 * in time order. Qhead points to the head of the list and addtoQ() 
 * adds a note to the list. The unused elements of array Q are held
 * in another linked list pointed to by freehead. The tail is pointed
 * to by freetail. removefromQ() removes an element (always from the
 * head of the list) and adds it to the free list. Qinit() initializes
 * the queue and clearQ() outputs all the remaining notes at the end
 * of a track.
 * Qcheck() and PrintQ() are diagnostic routines.
 */

#include <stdio.h>
#include <stdlib.h>
#include "queues.h"
#include "abc.h"
#include "genmidi.h"
#include "midifile.h"

/* queue for notes waiting to end */
/* allows us to do general polyphony */
#define QSIZE 50
struct Qitem {
  int delay;
  int pitch;
  int chan;
  int effect;  /* [SS] 2012-12-11 */
  int next;
};
struct Qitem Q[QSIZE+1];
int Qhead, freehead, freetail;
extern int totalnotedelay; /* from genmidi.c [SS] */
extern int notedelay;      /* from genmidi.c [SS] */
extern int bendvelocity;   /* from genmidi.c [SS] */
extern int bendacceleration; /* from genmidi.c [SS] */

/* routines to handle note queue */

/* genmidi.c communicates with queues.c mainly through the    */
/* functions addtoQ and timestep. The complexity comes in the */
/* handling of chords. When another note in a chord is passed,*/
/* addtoQ detemines whether other notes in the Q structure    */
/* overlap in time with this chord and modifies the delay item*/
/* of the note which finish later so that it is relative to the*/
/* end of the earlier note. Normally all notes in the chord end*/
/* at the same as specifiedy abc standard, so the delay of the*/
/* other notes cached in the Q structure should be set to zero.*/

void addtoQ(num, denom, pitch, chan, effect, d)
int num, denom, pitch, chan, d;
int effect; /* [SS] 2012-12-11 */
{
  int i, done;
  int wait;
  int *ptr;

  wait = ((div_factor*num)/denom) + d;
  /* find free space */
  if (freehead == -1) {
    /* printQ(); */
    event_error("Too many notes in chord - probably missing ']' or '+'");
    return;
  } else {
    i = freehead;
    freehead = Q[freehead].next;
  };
  Q[i].pitch = pitch;
  Q[i].chan = chan;
  Q[i].effect = effect;  /* [SS] 2012-12-11 */
  /* find place in queue */
  ptr = &Qhead;
  done = 0;
  while (!done) {
    if (*ptr == -1) {
      *ptr = i;
      Q[i].next = -1;
      Q[i].delay = wait;
      done = 1;
    } else {
      if (Q[*ptr].delay > wait) {
        Q[*ptr].delay = Q[*ptr].delay - wait -notedelay;
        if (Q[*ptr].delay < 0) Q[*ptr].delay = 0;
        Q[i].next = *ptr;
        Q[i].delay = wait;
        *ptr = i;
        done = 1;
      } else {
        wait = wait - Q[*ptr].delay;
        ptr = &Q[*ptr].next;
      };
    };
  };
}

void removefromQ(i)
int i;
{
  if (i == -1) {
    printQ();
    event_fatal_error("Internal error - nothing to remove from queue");
  } else {
    if (Q[Qhead].delay != 0) {
    printQ();
      event_fatal_error("Internal error - queue head has non-zero time");
    };
    Qhead = Q[i].next;
    Q[i].next = freehead;
    freehead = i;
  };
}

void clearQ()
{
  int time;
  int i;

  /* remove gchord requests */
  time = 0;
  while ((Qhead != -1) && (Q[Qhead].pitch == -1)) {
    time = time + Q[Qhead].delay;
    i = Qhead;
    Qhead = Q[i].next;
    Q[i].next = freehead;
    freehead = i;
  };
  if (Qhead != -1) {
    timestep(time, 1);
  };
  /* do any remaining note offs, but don't do chord request */
  while (Qhead != -1) {
    event_error("Sustained notes beyond end of track");
    timestep(Q[Qhead].delay+1, 1);
  };
timestep(25,0); /* to avoid transient artefacts at end of track */
}

void printQ()
{
  int t;

  printf("Qhead = %d freehead = %d freetail = %d\n", 
         Qhead, freehead, freetail);
  t = Qhead;
  printf("Q:");
  while (t != -1) {
    printf("p(%d)-%d->", Q[t].pitch, Q[t].delay);
    t = Q[t].next;
  };
  printf("\n");
}

void advanceQ(t)
int t;
{
  if (Qhead == -1) {
    event_error("Internal error - empty queue");
  } else {
    Q[Qhead].delay = Q[Qhead].delay - t;
  };
}

void Qinit()
{
  int i;

  /* initialize queue of notes waiting to finish */
  Qhead = -1;
  freehead = 0;
  for (i=0; i<QSIZE-1; i++) {
    Q[i].next = i + 1;
  };
  Q[QSIZE-1].next = -1;
  freetail = QSIZE-1;
}

void Qcheck()
{
  int qfree, qused;
  int nextitem;
  int used[QSIZE];
  int i;
  int failed;

  failed = 0;
  for (i=0; i<QSIZE; i++) {
    used[i] = 0;
  };
  qused = 0;
  nextitem = Qhead;
  while (nextitem != -1) {
    qused = qused + 1;
    used[nextitem] = 1;
    nextitem = Q[nextitem].next;
    if ((nextitem < -1) || (nextitem >= QSIZE)) {
      failed = 1;
      printf("Queue corrupted Q[].next = %d\n", nextitem);
    };
  };
  qfree = 0;
  nextitem = freehead;
  while (nextitem != -1) {
    qfree = qfree + 1;
    used[nextitem] = 1;
    nextitem = Q[nextitem].next;
    if ((nextitem < -1) || (nextitem >= QSIZE)) {
      failed = 1;
      printf("Free Queue corrupted Q[].next = %d\n", nextitem);
    };
  };
  if (qfree + qused < QSIZE) {
    failed = 1;
    printf("qfree = %d qused = %d\n", qused, qfree);
  };
  for (i=0; i<QSIZE; i++) {
    if (used[i] == 0) {
      printf("Not used element %d\n", i);
      failed = 1;
    };
  };
  if (Q[freetail].next != -1) {
    printf("freetail = %d, Q[freetail].next = %d\n", freetail, 
           Q[freetail].next);
  };
  if (failed == 1) {
    printQ();
    event_fatal_error("Qcheck failed");
  };
}


/* [SS] 2012-12-11 */
void note_effect() {
  int delta8;
  int pitchbend;
  char data[2];
  int i;
  int velocity;
  delta8 = delta_time/8;
  pitchbend = 8192; 
  velocity = bendvelocity;
  for (i=0;i<8;i++) {
     pitchbend = pitchbend + velocity;
     velocity = velocity + bendacceleration;
     if (pitchbend > 16383) pitchbend = 16383;
     if (pitchbend < 0) pitchbend = 0;
 
     data[0] = (char) (pitchbend&0x7f);
     data[1] = (char) ((pitchbend>>7)&0x7f);
     mf_write_midi_event(delta8,pitch_wheel,Q[Qhead].chan,data,2);
     delta_time -= delta8;
     }
  midi_noteoff(delta_time, Q[Qhead].pitch, Q[Qhead].chan);
  pitchbend = 8192;
  data[0] = (char) (pitchbend&0x7f);
  data[1] = (char) ((pitchbend>>7)&0x7f);
  mf_write_midi_event(delta_time,pitch_wheel,Q[Qhead].chan,data,2);
  }


/* timestep is called by delay() in genmidi.c typically at the */
/* end of a note, chord or rest. It is also called by clearQ in*/
/* this file. Timestep, is not only responsible for sending the*/
/* midi_noteoff command for any expired notes in the Q structure*/
/* but also maintains the delta_time global variable which is  */
/* shared with genmidi.c. Timestep also calls advanceQ() in   */
/* this file which updates all the delay variables for the items */
/* in the Q structure to reflect the current MIDI time. Timestep */
/* also calls removefromQ in this file which cleans out expired */
/* notes from the Q structure. To make things even more complicated*/
/* timestep runs the dogchords and the dodrums for bass/chordal */
/* and drum accompaniments by calling the function progress_sequence.*/
/* Dogchords and dodrums may also call addtoQ changing the contents*/
/* of the Q structure array. The tracklen variable in MIDI time */
/* units is also maintained here.                               */ 

/* new: delta_time_track0 is declared in queues.h like delta_time */

void timestep(t, atend)
int t;
int atend;
{
  int time;
  int headtime;

  time = t;
  /* process any notes waiting to finish */
  while ((Qhead != -1) && (Q[Qhead].delay < time)) {
    headtime = Q[Qhead].delay;
    delta_time = delta_time + (long) headtime;
    delta_time_track0 = delta_time_track0 + (long) headtime; /* [SS] 2010-06-27*/
    time = time - headtime;
    advanceQ(headtime);
    if (Q[Qhead].pitch == -1) {
      if (!atend) {
        progress_sequence(Q[Qhead].chan);
      };
    } else {
       if (Q[Qhead].effect == 0) {
          midi_noteoff(delta_time, Q[Qhead].pitch, Q[Qhead].chan);
          tracklen = tracklen + delta_time;
          delta_time = 0L;}
       else {
          note_effect();  /* [SS] 2012-12-11 */
          tracklen = tracklen + delta_time;
          delta_time = 0L;}
       };

    removefromQ(Qhead);
  };
  if (Qhead != -1) {
    advanceQ(time);
  };
  delta_time = delta_time + (long)time - totalnotedelay;
  delta_time_track0 = delta_time_track0 + (long)time - totalnotedelay; /* [SS] 2010-06-27*/
}

