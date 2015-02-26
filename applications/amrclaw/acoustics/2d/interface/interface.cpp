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

#include <amr_single_step.h>
#include <fc2d_clawpack46.H>
#include <fclaw2d_map.h>
#include <p4est_connectivity.h>

#include <amr_forestclaw.H>
#include <amr_utils.H>
#include <fclaw2d_map_query.h>

#include <fclaw_register.h>

#include "interface_user.H"

typedef struct user_options
{
    int example;
    double rhol;
    double cl;
    double rhor;
    double cr;

    int is_registered;

} user_options_t;

static void *
options_register_user (fclaw_app_t * app, void *package, sc_options_t * opt)
{
    user_options_t* user = (user_options_t*) package;

    /* [user] User options */
    sc_options_add_double (opt, 0, "rho", &user->rhol, 1.0, "[user] rho (left) [1]");
    sc_options_add_double (opt, 0, "bulk", &user->cl, 1.0, "[user] c (left) [1]");
    sc_options_add_double (opt, 0, "rho", &user->rhor, 4.0, "[user] rho (right) [4]");
    sc_options_add_double (opt, 0, "bulk", &user->cr, 0.5, "[user] c (right) [0.5]");

    user->is_registered = 1;
    return NULL;
}


static const fclaw_app_options_vtable_t options_vtable_user =
{
    options_register_user,
    NULL,
    NULL,
    NULL
};

static
void register_user_options (fclaw_app_t * app,
                            const char *configfile,
                            user_options_t* user)
{
    FCLAW_ASSERT (app != NULL);

    fclaw_app_options_register (app,"user", configfile, &options_vtable_user,
                                user);
}

void run_program(fclaw_app_t* app)
{
    sc_MPI_Comm            mpicomm;

    /* Mapped, multi-block domain */
    p4est_connectivity_t     *conn = NULL;
    fclaw2d_domain_t	     *domain;
    fclaw2d_map_context_t    *cont = NULL;

    fc2d_clawpack46_options_t  *clawpack_options;
    amr_options_t              *gparms;
    user_options_t             *user;

    mpicomm = fclaw_app_get_mpi_size_rank (app, NULL, NULL);

    clawpack_options = fc2d_clawpack46_get_options(app);
    gparms = fclaw_forestclaw_get_options(app);
    user = (user_options_t*) fclaw_app_get_user(app);

    /* ---------------------------------------------------------- */
    /* Use [ax,bx]x[ay,by] */
    conn = p4est_connectivity_new_unitsquare();
    cont = fclaw2d_map_new_nomap();

    domain = fclaw2d_domain_new_conn_map (mpicomm, gparms->minlevel, conn, cont);

    /* ---------------------------------------------------------- */
    fclaw2d_domain_list_levels(domain, FCLAW_VERBOSITY_INFO);
    fclaw2d_domain_list_neighbors(domain, FCLAW_VERBOSITY_DEBUG);

    /* ---------------------------------------------------------- */
    init_domain_data(domain);

    set_domain_parms(domain,gparms);
    fc2d_clawpack46_set_options(domain,clawpack_options);

    link_problem_setup(domain,fc2d_clawpack46_setprob);

    interface_link_solvers(domain);

    link_regrid_functions(domain,interface_patch_tag4refinement,
                          interface_patch_tag4coarsening);

    /* ---------------------------------------------------------- */
    amrinit(&domain);
    amrrun(&domain);
    amrreset(&domain);

    /* ---------------------------------------------------------- */
    fclaw2d_map_destroy(cont);
}

int
main (int argc, char **argv)
{
    fclaw_app_t *app;
    int first_arg;
    fclaw_exit_type_t vexit;

    /* Options */
    sc_options_t                  *options;
    user_options_t                suser_options, *user = &suser_options;

    int retval;

    /* Initialize application */
    app = fclaw_app_new (&argc, &argv, user);

    /* Register packages */
    fclaw_forestclaw_register(app,"fclaw_options.ini");
    fc2d_clawpack46_register(app,"fclaw_options.ini");

    /* User defined options (defined above) */
    register_user_options (app, "fclaw_options.ini", user);

    /* Read configuration file(s) */
    options = fclaw_app_get_options (app);
    retval = fclaw_options_read_from_file(options);
    vexit =  fclaw_app_options_parse (app, &first_arg,"fclaw_options.ini.used");

    link_app_to_clawpatch(app);

    if (!retval & !vexit)
    {
        run_program(app);
    }

    fclaw_forestclaw_destroy(app);
    fclaw_app_destroy (app);

    return 0;
}