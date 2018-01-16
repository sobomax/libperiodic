/*
 * Copyright (c) 2016-2018, sobomax
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/time.h>
#include <assert.h>
#include <math.h>
//#define PRD_DEBUG 1
#if defined(PRD_DEBUG)
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "elperiodic.h"
#include "prdic_math.h"
#include "prdic_timespecops.h"

struct prdic_band {
    int id;
    double freq_hz;
    struct timespec period;
    struct timespec tfreq_hz;
    struct timespec epoch;
    struct recfilter loop_error;
    struct PFD phase_detector;
    struct timespec last_tclk;
    struct prdic_band *next;
};

struct prdic_inst {
    struct prdic_band bands[1];
    struct prdic_band *ab;
};

static inline int
getttime(struct timespec *ttp, int abort_on_fail)
{

    if (clock_gettime(CLOCK_MONOTONIC, ttp) == -1) {
        if (abort_on_fail)
            abort();
        return (-1);
    }
    return (0);
}

#if 0
static void
dtime2timespec(double dtime, struct timespec *ttp)
{

    SEC(ttp) = trunc(dtime);
    dtime -= (double)SEC(ttp);
    NSEC(ttp) = round((double)NSEC_IN_SEC * dtime);
}
#endif

static void
tplusdtime(struct timespec *ttp, double offset)
{
    struct timespec tp;

    dtime2timespec(offset, &tp);
    timespecadd(ttp, &tp);
}

static void
band_init(struct prdic_band *bp, double freq_hz)
{

    bp->freq_hz = freq_hz;
    dtime2timespec(1.0 / freq_hz, &bp->period);
    dtime2timespec(freq_hz, &bp->tfreq_hz);
    recfilter_init(&bp->loop_error, 0.96, 0.0, 0);
    PFD_init(&bp->phase_detector);
}

void *
prdic_init(double freq_hz, double off_from_now)
{
    struct prdic_inst *pip;

    pip = malloc(sizeof(struct prdic_inst));
    if (pip == NULL) {
        goto e0;
    }
    memset(pip, '\0', sizeof(struct prdic_inst));
    pip->ab = &pip->bands[0];
    if (getttime(&pip->ab->epoch, 0) != 0) {
        goto e1;
    }
    tplusdtime(&pip->ab->epoch, off_from_now);
    band_init(pip->ab, freq_hz);
    return ((void *)pip);
e1:
    free(pip);
e0:
    return (NULL);
}

int
prdic_addband(void *prdic_inst, double freq_hz)
{
    struct prdic_inst *pip;
    struct prdic_band *bp, *tbp;
    int i;

    pip = (struct prdic_inst *)prdic_inst;

    bp = malloc(sizeof(struct prdic_band));
    if (bp == NULL)
        return (-1);
    memset(bp, '\0', sizeof(struct prdic_band));
    bp->epoch = pip->bands[0].epoch;
    band_init(bp, freq_hz);
    for (tbp = &pip->bands[0]; tbp->next != NULL; tbp = tbp->next)
        continue;
    bp->id = tbp->id + 1;
    assert(tbp->next == NULL);
    tbp->next = bp;
    return (bp->id);
}

static void
band_set_epoch(struct prdic_band *bp, struct timespec *epoch)
{

    bp->epoch = *epoch;
    SEC(&bp->phase_detector.target_tclk) = 0;
    NSEC(&bp->phase_detector.target_tclk) = 0;
}

void
prdic_useband(void *prdic_inst, int bnum)
{
    struct prdic_inst *pip;
    struct prdic_band *bp, *tbp;
    int i;
    struct timespec nepoch, tepoch;

    pip = (struct prdic_inst *)prdic_inst;

    if (bnum == pip->ab->id)
        return;

    for (tbp = &pip->bands[0]; tbp != NULL; tbp = tbp->next) {
        if (tbp->id == bnum)
            break;
    }
    assert(tbp != NULL); /* prdic_useband() requested band is not found */
    SEC(&tepoch) = SEC(&pip->ab->last_tclk);
    NSEC(&tepoch) = 0;
    timespecmul(&nepoch, &tepoch, &pip->ab->period);
    timespecadd(&nepoch, &pip->ab->epoch);
    band_set_epoch(tbp, &nepoch);
    pip->ab = tbp;
}

int
prdic_procrastinate(void *prdic_inst)
{
    struct prdic_inst *pip;
    struct timespec tsleep, tremain;
    int rval;
    double add_delay, eval;
    struct timespec eptime;

    pip = (struct prdic_inst *)prdic_inst;

    add_delay = freqoff_to_period(pip->ab->freq_hz, 1.0, pip->ab->loop_error.lastval);
    dtime2timespec(add_delay, &tremain);

    do {
        tsleep = tremain;
        memset(&tremain, '\0', sizeof(tremain));
        rval = nanosleep(&tsleep, &tremain);
    } while (rval < 0 && !timespeciszero(&tremain));

    getttime(&eptime, 1);

    timespecsub(&eptime, &pip->ab->epoch);
    timespecmul(&pip->ab->last_tclk, &eptime, &pip->ab->tfreq_hz);

    eval = PFD_get_error(&pip->ab->phase_detector, &pip->ab->last_tclk);

#if defined(PRD_DEBUG)
    fprintf(stderr, "error=%f\n", eval);
    if (eval == 0.0 || 1) {
        fprintf(stderr, "last=%lld target=%lld\n", SEC(&pip->ab->last_tclk), SEC(&pip->ab->phase_detector.target_tclk));
    }
    fflush(stderr);
#endif

    if (eval != 0.0) {
        recfilter_apply(&pip->ab->loop_error, sigmoid(eval));
    }
    return (0);
}

void
prdic_set_fparams(void *prdic_inst, double fcoef)
{
    struct prdic_inst *pip;

    pip = (struct prdic_inst *)prdic_inst;
    assert(pip->ab->loop_error.lastval == 0.0);
    recfilter_adjust(&pip->ab->loop_error, fcoef);
}

void
prdic_set_epoch(void *prdic_inst, struct timespec *tp)
{
    struct prdic_inst *pip;

    pip = (struct prdic_inst *)prdic_inst;
    band_set_epoch(pip->ab, tp);
}

time_t
prdic_getncycles_ref(void *prdic_inst)
{
    struct prdic_inst *pip;

    pip = (struct prdic_inst *)prdic_inst;
    return (SEC(&pip->ab->last_tclk));
}

void
prdic_free(void *prdic_inst)
{
    struct prdic_inst *pip;
    struct prdic_band *tbp, *fbp;

    pip = (struct prdic_inst *)prdic_inst;
    for (tbp = pip->bands[0].next; tbp != NULL;) {
        fbp = tbp;
        tbp = tbp->next;
        free(fbp);
    }
    free(prdic_inst);
}
