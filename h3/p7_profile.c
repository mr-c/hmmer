/* Routines for the P7_PROFILE structure - a Plan 7 search profile
 *                                         
 *    1. The P7_PROFILE object: allocation, initialization, destruction.
 *    2. Access methods.
 *    3. Debugging and development code.
 *    4. MPI communication
 *    
 * SRE, Thu Jan 11 15:16:47 2007 [Janelia] [Sufjan Stevens, Illinois]
 * SVN $Id$
 */

#include "p7_config.h"

#include "easel.h"
#include "esl_vectorops.h"

#include "hmmer.h"


/*****************************************************************
 * 1. The P7_PROFILE object: allocation, initialization, destruction.
 *****************************************************************/

/* Function:  p7_profile_Create()
 * Incept:    SRE, Thu Jan 11 15:53:28 2007 [Janelia]
 *
 * Purpose:   Creates a profile of <M> nodes, for digital alphabet <abc>.
 *
 * Returns:   a pointer to the new profile.
 *
 * Throws:    NULL on allocation error.
 *
 * Xref:      STL11/125.
 */
P7_PROFILE *
p7_profile_Create(int M, ESL_ALPHABET *abc)
{
  P7_PROFILE *gm = NULL;
  int         x;
  int         status;

  /* level 0 */
  ESL_ALLOC(gm, sizeof(P7_PROFILE));
  gm->tsc   = gm->msc = gm->isc = NULL;
  gm->bsc   = gm->esc = NULL;
  gm->begin = gm->end = NULL;
  
  /* level 1 */
  ESL_ALLOC(gm->tsc, sizeof(int *) * 7);       gm->tsc[0] = NULL;
  ESL_ALLOC(gm->msc, sizeof(int *) * abc->Kp); gm->msc[0] = NULL;
  ESL_ALLOC(gm->isc, sizeof(int *) * abc->Kp); gm->isc[0] = NULL;
  ESL_ALLOC(gm->bsc, sizeof(int) * (M+1));      
  ESL_ALLOC(gm->esc, sizeof(int) * (M+1));      

  /* Begin, end may eventually disappear in production
   * code, but we need them in research code for now to
   * be able to emulate & test HMMER2 configurations.
   */
  ESL_ALLOC(gm->begin, sizeof(float) * (M+1));
  ESL_ALLOC(gm->end,   sizeof(float) * (M+1));
  
  /* level 2 */
  ESL_ALLOC(gm->tsc[0], sizeof(int) * 7*M);    
  ESL_ALLOC(gm->msc[0], sizeof(int) * abc->Kp * (M+1));
  ESL_ALLOC(gm->isc[0], sizeof(int) * abc->Kp * M);
  for (x = 1; x < abc->Kp; x++) {
    gm->msc[x] = gm->msc[0] + x * (M+1);
    gm->isc[x] = gm->isc[0] + x * M;
  }
  for (x = 0; x < 7; x++)
    gm->tsc[x] = gm->tsc[0] + x * M;

  /* Initialize some pieces of memory that are never used,
   * only there for indexing convenience.
   */
  gm->tsc[p7_TMM][0] = p7_IMPOSSIBLE; /* node 0 nonexistent, has no transitions  */
  gm->tsc[p7_TMI][0] = p7_IMPOSSIBLE;
  gm->tsc[p7_TMD][0] = p7_IMPOSSIBLE;
  gm->tsc[p7_TIM][0] = p7_IMPOSSIBLE;
  gm->tsc[p7_TII][0] = p7_IMPOSSIBLE;
  gm->tsc[p7_TDM][0] = p7_IMPOSSIBLE;
  gm->tsc[p7_TDD][0] = p7_IMPOSSIBLE;
  gm->tsc[p7_TDM][1] = p7_IMPOSSIBLE; /* delete state D_1 is wing-retracted */
  gm->tsc[p7_TDD][1] = p7_IMPOSSIBLE;
  for (x = 0; x < abc->Kp; x++) {     /* no emissions from nonexistent M_0, I_0 */
    gm->msc[x][0] = p7_IMPOSSIBLE;
    gm->isc[x][0] = p7_IMPOSSIBLE;
  }
  x = esl_abc_XGetGap(abc);	      /* no emission can emit/score gap characters */
  esl_vec_ISet(gm->msc[x], M+1, p7_IMPOSSIBLE);
  esl_vec_ISet(gm->isc[x], M,   p7_IMPOSSIBLE);
  x = esl_abc_XGetMissing(abc);	      /* no emission can emit/score missing data characters */
  esl_vec_ISet(gm->msc[x], M+1, p7_IMPOSSIBLE);
  esl_vec_ISet(gm->isc[x], M,   p7_IMPOSSIBLE);

  /* Set remaining info
   */
  gm->mode        = p7_NO_MODE;
  gm->M           = M;
  gm->abc         = abc;
  gm->hmm         = NULL;
  gm->bg          = NULL;
  gm->do_lcorrect = FALSE;
  gm->lscore      = 0.;
  gm->h2_mode     = FALSE;
  return gm;

 ERROR:
  p7_profile_Destroy(gm);
  return NULL;
}

/* Function:  p7_profile_Destroy()
 * Incept:    SRE, Thu Jan 11 15:54:17 2007 [Janelia]
 *
 * Purpose:   Frees a profile <gm>.
 *
 * Returns:   (void).
 *
 * Xref:      STL11/125.
 */
void
p7_profile_Destroy(P7_PROFILE *gm)
{
  if (gm != NULL) {
    if (gm->tsc   != NULL && gm->tsc[0] != NULL) free(gm->tsc[0]);
    if (gm->msc   != NULL && gm->msc[0] != NULL) free(gm->msc[0]);
    if (gm->isc   != NULL && gm->isc[0] != NULL) free(gm->isc[0]);
    if (gm->tsc   != NULL) free(gm->tsc);
    if (gm->msc   != NULL) free(gm->msc);
    if (gm->isc   != NULL) free(gm->isc);
    if (gm->bsc   != NULL) free(gm->bsc);
    if (gm->esc   != NULL) free(gm->esc);
    if (gm->begin != NULL) free(gm->begin);
    if (gm->end   != NULL) free(gm->end);
  }
  free(gm);
  return;
}

/*****************************************************************
 * 2. Access methods.
 *****************************************************************/


/* Function:  p7_profile_GetTScore()
 * Incept:    SRE, Wed Apr 12 14:20:18 2006 [St. Louis]
 *
 * Purpose:   Convenience function that looks up a transition score in
 *            profile <gm> for a transition from state type <st1> in
 *            node <k1> to state type <st2> in node <k2>. For unique
 *            state types that aren't in nodes (<p7_STS>, for example), the
 *            <k> value is ignored, though it would be customarily passed as 0.
 *            Return the transition score in <ret_tsc>.
 *            
 * Returns:   <eslOK> on success, and <*ret_tsc> contains the requested
 *            transition score.            
 * 
 * Throws:    <eslEINVAL> if a nonexistent transition is requested. Now
 *            <*ret_tsc> is set to 0.
 */
int
p7_profile_GetT(P7_PROFILE *gm, char st1, int k1, char st2, int k2, int *ret_tsc)
{
  int status;
  int tsc    = 0;

  switch (st1) {
  case p7_STS:  break;
  case p7_STT:  break;

  case p7_STN:
    switch (st2) {
    case p7_STB: tsc =  gm->xsc[p7_XTN][p7_MOVE]; break;
    case p7_STN: tsc =  gm->xsc[p7_XTN][p7_LOOP]; break;
    default:     ESL_XEXCEPTION(eslEINVAL, "bad transition %s->%s", 
				p7_hmm_DescribeStatetype(st1),
				p7_hmm_DescribeStatetype(st2));
    }
    break;

  case p7_STB:
    switch (st2) {
    case p7_STM: tsc = gm->bsc[k2]; break;
    default:     ESL_XEXCEPTION(eslEINVAL, "bad transition %s->%s", 
				p7_hmm_DescribeStatetype(st1),
				p7_hmm_DescribeStatetype(st2));
    }
    break;

  case p7_STM:
    switch (st2) {
    case p7_STM: tsc = gm->tsc[p7_TMM][k1]; break;
    case p7_STI: tsc = gm->tsc[p7_TMI][k1]; break;
    case p7_STD: tsc = gm->tsc[p7_TMD][k1]; break;
    case p7_STE: tsc = gm->esc[k1];         break;
    default:     ESL_XEXCEPTION(eslEINVAL, "bad transition %s_%d->%s", 
				p7_hmm_DescribeStatetype(st1), k1,
				p7_hmm_DescribeStatetype(st2));
    }
    break;

  case p7_STD:
    switch (st2) {
    case p7_STM: tsc = gm->tsc[p7_TDM][k1]; break;
    case p7_STD: tsc = gm->tsc[p7_TDD][k1]; break;
    default:     ESL_XEXCEPTION(eslEINVAL, "bad transition %s_%d->%s", 
				p7_hmm_DescribeStatetype(st1), k1,
				p7_hmm_DescribeStatetype(st2));
    }
    break;

  case p7_STI:
    switch (st2) {
    case p7_STM: tsc = gm->tsc[p7_TIM][k1]; break;
    case p7_STI: tsc = gm->tsc[p7_TII][k1]; break;
    default:     ESL_XEXCEPTION(eslEINVAL, "bad transition %s_%d->%s", 
				p7_hmm_DescribeStatetype(st1), k1,
				p7_hmm_DescribeStatetype(st2));
    }
    break;

  case p7_STE:
    switch (st2) {
    case p7_STC: tsc = gm->xsc[p7_XTE][p7_MOVE]; break;
    case p7_STJ: tsc = gm->xsc[p7_XTE][p7_LOOP]; break;
    default:     ESL_XEXCEPTION(eslEINVAL, "bad transition %s->%s", 
				p7_hmm_DescribeStatetype(st1),
				p7_hmm_DescribeStatetype(st2));
    }
    break;

  case p7_STJ:
    switch (st2) {
    case p7_STB: tsc = gm->xsc[p7_XTJ][p7_MOVE]; break;
    case p7_STJ: tsc = gm->xsc[p7_XTJ][p7_LOOP]; break;
    default:     ESL_XEXCEPTION(eslEINVAL, "bad transition %s->%s", 
				p7_hmm_DescribeStatetype(st1),
				p7_hmm_DescribeStatetype(st2));
    }
    break;

  case p7_STC:
    switch (st2) {
    case p7_STT:  tsc = gm->xsc[p7_XTC][p7_MOVE]; break;
    case p7_STC:  tsc = gm->xsc[p7_XTC][p7_LOOP]; break;
    default:     ESL_XEXCEPTION(eslEINVAL, "bad transition %s->%s", 
				p7_hmm_DescribeStatetype(st1),
				p7_hmm_DescribeStatetype(st2));
    }
    break;

  default: ESL_XEXCEPTION(eslEINVAL, "bad state type %d in traceback", st1);
  }

  *ret_tsc = tsc;
  return eslOK;

 ERROR:
  *ret_tsc = 0;
  return status;
}


/*****************************************************************
 * 3. Debugging and development code.
 *****************************************************************/

/* Function:  p7_profile_Validate()
 * Incept:    SRE, Tue Jan 23 13:58:04 2007 [Janelia]
 *
 * Purpose:   Validates the internals of the generic profile structure
 *            <gm>. Probability vectors in the implicit profile
 *            probabilistic model are validated to sum to 1.0 +/- <tol>.
 *            
 *            TODO: currently this function only validates the implicit
 *            model's probabilities, nothing else.
 *            
 * Returns:   <eslOK> if <gm> internals look fine. Returns <eslFAIL>
 *            if something is wrong.
 */
int
p7_profile_Validate(P7_PROFILE *gm, float tol)
{
  float sum;
  int k,i;

  /* begin[k] should sum to 1.0 over the M(M+1)/2 entries in
   * the implicit model
   */
  for (sum = 0., k = 1; k <= gm->M; k++)
    sum += gm->begin[k] * (gm->M - k + 1);
  if (esl_FCompare(sum, 1.0, tol) != eslOK) return eslFAIL;

  /* end[k] should all be 1.0 in the implicit model
   */
  for (k = 1; k <= gm->M; k++)
    if (gm->end[k] != 1.0) return eslFAIL;

  /* all four xt's should sum to 1.0
   */
  for (i = 0; i < 4; i++)
    if (esl_FCompare(gm->xt[i][p7_MOVE] + gm->xt[i][p7_LOOP], 1.0, tol) != eslOK) return eslFAIL;

  return eslOK;
}


/*****************************************************************
 * 4. MPI communication
 *****************************************************************/
#ifdef HAVE_MPI
#include "mpi.h"

/* Function:  p7_profile_MPISend()
 * Incept:    SRE, Fri Apr 20 13:55:47 2007 [Janelia]
 *
 * Purpose:   Sends profile <gm> to processor <dest>.
 *            
 *            If <gm> is NULL, sends a end-of-data signal to <dest>, to
 *            tell it to shut down.
 *            
 */
int
p7_profile_MPISend(P7_PROFILE *gm, int dest)
{
  
  if (gm == NULL) {
    int eod = -1;
    MPI_Send(&eod, 1, MPI_INT, dest, 0, MPI_COMM_WORLD);
    return eslOK;
  }

  MPI_Send(&(gm->M), 1, MPI_INT, dest, 0, MPI_COMM_WORLD);
  /* receiver will now allocate storage, before reading on...*/
  MPI_Send(&(gm->mode),                    1, MPI_INT,   dest, 0, MPI_COMM_WORLD);
  MPI_Send(gm->tsc[0],               7*gm->M, MPI_INT,   dest, 0, MPI_COMM_WORLD);
  MPI_Send(gm->msc[0], (gm->M+1)*gm->abc->Kp, MPI_INT,   dest, 0, MPI_COMM_WORLD);
  MPI_Send(gm->isc[0],     gm->M*gm->abc->Kp, MPI_INT,   dest, 0, MPI_COMM_WORLD);
  MPI_Send(gm->xsc[0],                     2, MPI_INT,   dest, 0, MPI_COMM_WORLD);
  MPI_Send(gm->xsc[1],                     2, MPI_INT,   dest, 0, MPI_COMM_WORLD);
  MPI_Send(gm->xsc[2],                     2, MPI_INT,   dest, 0, MPI_COMM_WORLD);
  MPI_Send(gm->xsc[3],                     2, MPI_INT,   dest, 0, MPI_COMM_WORLD);
  MPI_Send(gm->bsc,                  gm->M+1, MPI_INT,   dest, 0, MPI_COMM_WORLD);
  MPI_Send(gm->esc,                  gm->M+1, MPI_INT,   dest, 0, MPI_COMM_WORLD);
  MPI_Send(gm->xt[0],                      2, MPI_FLOAT, dest, 0, MPI_COMM_WORLD);
  MPI_Send(gm->xt[1],                      2, MPI_FLOAT, dest, 0, MPI_COMM_WORLD);
  MPI_Send(gm->xt[2],                      2, MPI_FLOAT, dest, 0, MPI_COMM_WORLD);
  MPI_Send(gm->xt[3],                      2, MPI_FLOAT, dest, 0, MPI_COMM_WORLD);
  MPI_Send(gm->begin,                gm->M+1, MPI_FLOAT, dest, 0, MPI_COMM_WORLD);
  MPI_Send(gm->end,                  gm->M+1, MPI_FLOAT, dest, 0, MPI_COMM_WORLD);
  MPI_Send(&(gm->do_lcorrect),             1, MPI_INT,   dest, 0, MPI_COMM_WORLD);
  MPI_Send(&(gm->lscore),                  1, MPI_FLOAT, dest, 0, MPI_COMM_WORLD);
  MPI_Send(&(gm->h2_mode),                 1, MPI_INT,   dest, 0, MPI_COMM_WORLD);
  return eslOK;
}

/* Function:  p7_profile_MPIRecv()
 * Incept:    SRE, Fri Apr 20 14:19:07 2007 [Janelia]
 *
 * Purpose:   Receive a profile sent from the master MPI process (src=0)
 *            on a worker MPI process. The worker must already have (and
 *            provide) the alphabet <abc> and the background model <bg>.
 *            
 *            If it receives an end-of-data signal, returns <eslEOD>.
 */
int
p7_profile_MPIRecv(ESL_ALPHABET *abc, P7_BG *bg, P7_PROFILE **ret_gm)
{
  P7_PROFILE *gm = NULL;
  MPI_Status mpistatus;
  int M;

  MPI_Recv(&M, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &mpistatus);
  if (M == -1) return eslEOD;

  gm = p7_profile_Create(M, abc);
  MPI_Recv(&(gm->mode),             1, MPI_INT,   0, 0, MPI_COMM_WORLD, &mpistatus);
  MPI_Recv(gm->tsc[0],            7*M, MPI_INT,   0, 0, MPI_COMM_WORLD, &mpistatus);
  MPI_Recv(gm->msc[0],  (M+1)*abc->Kp, MPI_INT,   0, 0, MPI_COMM_WORLD, &mpistatus);
  MPI_Recv(gm->isc[0],      M*abc->Kp, MPI_INT,   0, 0, MPI_COMM_WORLD, &mpistatus);
  MPI_Recv(gm->xsc[0],              2, MPI_INT,   0, 0, MPI_COMM_WORLD, &mpistatus);  
  MPI_Recv(gm->xsc[1],              2, MPI_INT,   0, 0, MPI_COMM_WORLD, &mpistatus);  
  MPI_Recv(gm->xsc[2],              2, MPI_INT,   0, 0, MPI_COMM_WORLD, &mpistatus);  
  MPI_Recv(gm->xsc[3],              2, MPI_INT,   0, 0, MPI_COMM_WORLD, &mpistatus);  
  MPI_Recv(gm->bsc,               M+1, MPI_INT,   0, 0, MPI_COMM_WORLD, &mpistatus);
  MPI_Recv(gm->esc,               M+1, MPI_INT,   0, 0, MPI_COMM_WORLD, &mpistatus);
  MPI_Recv(gm->xt[0],               2, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, &mpistatus);  
  MPI_Recv(gm->xt[1],               2, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, &mpistatus);  
  MPI_Recv(gm->xt[2],               2, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, &mpistatus);  
  MPI_Recv(gm->xt[3],               2, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, &mpistatus);  
  MPI_Recv(gm->begin,             M+1, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, &mpistatus);
  MPI_Recv(gm->end,               M+1, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, &mpistatus);
  MPI_Recv(&(gm->do_lcorrect),      1, MPI_INT,   0, 0, MPI_COMM_WORLD, &mpistatus);
  MPI_Recv(&(gm->lscore),           1, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, &mpistatus);
  MPI_Recv(&(gm->h2_mode),          1, MPI_INT,   0, 0, MPI_COMM_WORLD, &mpistatus);  
  
  gm->abc = abc;
  gm->hmm = NULL;
  gm->bg  = bg;

  *ret_gm = gm;
  return eslOK;
}

#endif /*HAVE_MPI*/

/*****************************************************************
 * @LICENSE@
 *****************************************************************/
