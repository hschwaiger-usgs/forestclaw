/*
Copyright (c) 2012 Carsten Burstedde, Donna Calhoun
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <fclaw2d_forestclaw.h>
#include <fclaw2d_clawpatch.h>
#include <fclaw2d_domain.h>
#include <fclaw2d_regrid.h>
#include <fclaw2d_vtable.h>
#include <fclaw2d_partition.h>
#include <fclaw2d_exchange.h>


/* Also needed in amrreset */
fclaw2d_domain_exchange_t*
    fclaw2d_exchange_get_data(fclaw2d_domain_t* domain)
{
    fclaw2d_domain_data_t *ddata = fclaw2d_domain_get_data (domain);
    return ddata->domain_exchange;
}

static
void set_exchange_data(fclaw2d_domain_t* domain,
                       fclaw2d_domain_exchange_t *e)
{
    fclaw2d_domain_data_t *ddata = fclaw2d_domain_get_data (domain);
    ddata->domain_exchange = e;
}

static
void build_ghost_patches(fclaw2d_domain_t* domain)
{
    const amr_options_t *gparms = get_domain_parms(domain);

    fclaw_infof("[%d] Number of ghost patches : %d\n",
                            domain->mpirank,domain->num_ghost_patches);
    for(int i = 0; i < domain->num_ghost_patches; i++)
    {
        fclaw2d_patch_t* ghost_patch = &domain->ghost_patches[i];

        int blockno = ghost_patch->u.blockno;

        /* not clear how useful this patchno is.  In any case, it isn't
           used in defining the ClawPatch, so probably doesn't
           need to be passed in */
        int patchno = i;

        fclaw2d_build_mode_t build_mode;
        if (gparms->ghost_patch_pack_area)
        {
            build_mode = FCLAW2D_BUILD_FOR_GHOST_AREA_PACKED;
        }
        else
        {
            build_mode = FCLAW2D_BUILD_FOR_GHOST_AREA_COMPUTED;
        }

        fclaw2d_patch_data_new(domain,ghost_patch);
        fclaw2d_clawpatch_build(domain,ghost_patch,blockno,
                                patchno,(void*) &build_mode);
    }
}

static
void delete_ghost_patches(fclaw2d_domain_t* domain)
{
    for(int i = 0; i < domain->num_ghost_patches; i++)
    {
        fclaw2d_patch_t* ghost_patch = &domain->ghost_patches[i];
        fclaw2d_patch_data_delete(domain,ghost_patch);
    }
}


static void
unpack_ghost_patches(fclaw2d_domain_t* domain,
                     fclaw2d_domain_exchange_t *e,
                     int minlevel,
                     int maxlevel,
                     int time_interp)
{
    for(int i = 0; i < domain->num_ghost_patches; i++)
    {
        fclaw2d_patch_t* ghost_patch = &domain->ghost_patches[i];
        int level = ghost_patch->level;

        if (level >= minlevel-1)
        {
            int blockno = ghost_patch->u.blockno;

            int patchno = i;

            /* access data stored on remote procs. */
            double *q = (double*) e->ghost_data[patchno];

            int unpack_to_timeinterp_patch=0;
            if (time_interp && level == minlevel-1)
            {
                unpack_to_timeinterp_patch = 1;
            }
            fclaw2d_clawpatch_ghost_unpack(domain, ghost_patch, blockno,
                                           patchno, q, unpack_to_timeinterp_patch);
        }
    }
}

/* --------------------------------------------------------------------------
   Public interface
   -------------------------------------------------------------------------- */
/* This is called by rebuild_domain */
void fclaw2d_exchange_setup(fclaw2d_domain* domain)
{
    size_t data_size =  fclaw2d_clawpatch_ghost_packsize(domain);
    fclaw2d_domain_exchange_t *e;

    /* we just created a grid by amrinit or regrid and we now need to
       allocate data to store and retrieve local boundary patches and
       remote ghost patches */
    e = fclaw2d_domain_allocate_before_exchange (domain, data_size);

    /* Store locations of on-proc boundary patches that will be communicated
       to neighboring remote procs.  The pointer stored in e->patch_data[]
       will point to either the patch data itself, or to a newly allocated
       contiguous memory block that stores qdata and area (for computations
       on manifolds).*/
    int zz = 0;
    for (int nb = 0; nb < domain->num_blocks; ++nb)
    {
        for (int np = 0; np < domain->blocks[nb].num_patches; ++np)
        {
            if (domain->blocks[nb].patches[np].flags &
                FCLAW2D_PATCH_ON_PARALLEL_BOUNDARY)
            {
                /* Copy q and area into one contingous block */
                fclaw2d_patch_t *this_patch = &domain->blocks[nb].patches[np];
                fclaw2d_clawpatch_ghost_pack_location(domain,this_patch,
                                                      &e->patch_data[zz++]);
            }
        }
    }

    /* Build ghost patches from neighboring remote processors.  These will be
       filled later with q data and the area, if we are on a manifold */
    build_ghost_patches(domain);

    /* Store e so we can retrieve it later */
    set_exchange_data(domain,e);
}

void fclaw2d_exchange_delete(fclaw2d_domain_t** domain)
{
    /* Free old parallel ghost patch data structure, must exist by construction. */
    fclaw2d_domain_data_t *ddata = fclaw2d_domain_get_data (*domain);
    fclaw2d_domain_exchange_t *e_old = ddata->domain_exchange;

    /* Free contiguous memory, if allocated.  If no memory was allocated
       (because we are not storing the area), nothing de-allocated. */
    if (e_old != NULL)
    {
        /* Delete local patches which are passed to other procs */
        int zz = 0;
        for (int nb = 0; nb < (*domain)->num_blocks; ++nb)
        {
            for (int np = 0; np < (*domain)->blocks[nb].num_patches; ++np)
            {
                if ((*domain)->blocks[nb].patches[np].flags &
                    FCLAW2D_PATCH_ON_PARALLEL_BOUNDARY)
                {
                    fclaw2d_clawpatch_ghost_free_pack_location(*domain,
                                                               &e_old->patch_data[zz++]);
                }
            }
        }
    }

    /* Delete ghost patches from remote neighboring patches */
    delete_ghost_patches(*domain);
    fclaw2d_domain_free_after_exchange (*domain, e_old);
}


/* ----------------------------------------------------------------
   Public interface
   -------------------------------------------------------------- */

/* This is called whenever all time levels are time synchronized. */
void fclaw2d_exchange_ghost_patches_begin(fclaw2d_domain_t* domain,
                                          int minlevel,
                                          int maxlevel,
                                          int time_interp)
{
    fclaw2d_domain_data_t *ddata = fclaw2d_domain_get_data (domain);
    fclaw2d_domain_exchange_t *e = fclaw2d_exchange_get_data(domain);

    /* Pack local data into on-proc patches at the parallel boundary that
       will be shipped of to other processors. */
    int zz = 0;
    for (int nb = 0; nb < domain->num_blocks; ++nb)
    {
        for (int np = 0; np < domain->blocks[nb].num_patches; ++np)
        {
            if (domain->blocks[nb].patches[np].flags &
                FCLAW2D_PATCH_ON_PARALLEL_BOUNDARY)
            {
                fclaw2d_patch_t *this_patch = &domain->blocks[nb].patches[np];
                int level = this_patch->level;
                FCLAW_ASSERT(level <= maxlevel);

                int pack_time_interp = time_interp && level == minlevel-1;

                /* Pack q and area into one contingous block */
                fclaw2d_clawpatch_ghost_pack(domain,this_patch,
                                             (double*) e->patch_data[zz++],
                                             pack_time_interp);
            }
        }
    }

    /* Exchange only over levels currently in use */
    fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_GHOSTCOMM_BEGIN]);
    if (time_interp)
    {
        int time_interp_level = minlevel-1;

        fclaw2d_domain_ghost_exchange_begin
            (domain, e,time_interp_level, maxlevel);
    }
    else
    {
        fclaw2d_domain_ghost_exchange_begin(domain, e, minlevel, maxlevel);

    }
    fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_GHOSTCOMM_BEGIN]);

}

/* This is called whenever all time levels are time synchronized. */
void fclaw2d_exchange_ghost_patches_end(fclaw2d_domain_t* domain,
                                        int minlevel,
                                        int maxlevel,
                                        int time_interp)
{
    fclaw2d_domain_data_t *ddata = fclaw2d_domain_get_data (domain);
    fclaw2d_domain_exchange_t *e = fclaw2d_exchange_get_data(domain);

    /* Exchange only over levels currently in use */
    fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_GHOSTCOMM_END]);
    if (time_interp)
    {
        fclaw2d_domain_ghost_exchange_end (domain, e);
    }
    else
    {
        fclaw2d_domain_ghost_exchange_end (domain, e);
    }
    fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_GHOSTCOMM_END]);

    /* Unpack data from remote patches to corresponding ghost patches
       stored locally */
    unpack_ghost_patches(domain,e,minlevel,maxlevel,time_interp);

    /* Count calls to this function */
    ++ddata->count_ghost_exchange;
}
