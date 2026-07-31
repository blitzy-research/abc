/**CFile****************************************************************

  FileName    [giaHash.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [Scalable AIG package.]

  Synopsis    [Structural hashing.]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - June 20, 2005.]

  Revision    [$Id: giaHash.c,v 1.00 2005/06/20 00:00:00 alanmi Exp $]

***********************************************************************/

#include "gia.h"

ABC_NAMESPACE_IMPL_START


////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    [Returns the place where this node is stored (or should be stored).]

  Description [These two functions are the whole of the lookup, and neither
  reads or writes a node. Gia_ManHashOne is the key: it mixes the two fanin
  variables, the two fanin complement bits and the optional control literal
  into an unsigned accumulator under five distinct odd multipliers, then
  reduces it modulo the caller's bucket count. A literal therefore contributes
  twice, once as a variable and once as an inversion, which is what sends
  AND(a,b) and AND(a,!b) to different buckets. The accumulator is unsigned, so
  the products wrap rather than overflow. Callers that hash no control literal
  pass iLitC as -1, and that value is not arbitrary: it is exactly what
  Gia_ObjFaninLit2 and Gia_ObjFaninLit2p in gia.h yield for an object with no
  stored control, so one convention covers both the key and the comparison.
  Gia_ManHashFind returns the address of a table slot rather than a node. It
  takes the bucket head from p->vHTable and follows next links held in
  p->vHash, an integer vector indexed by object identifier, stopping at the
  first entry whose two fanin literals - and, once p->pMuxes exists, whose
  stored control literal - match. On a hit the returned address holds the
  matching object identifier; on a miss it is the zero slot that terminates
  the chain, which is where a caller writes a new entry. So the chain lives
  entirely in the manager, in those two vectors, and no part of it is kept in
  the object. Four assertions state the layout and the canonical form the walk
  depends on: one vHash entry per object, which Gia_ManAppendObj in gia.h
  maintains by pushing a zero for each new object while the table is live;
  ascending fanin literals unless p->pMuxes is present; both fanins
  uncomplemented whenever the literals are not ascending, which is the form
  Gia_ManHashXorReal produces; and an uncomplemented second data literal
  whenever a control literal is given.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
static inline int Gia_ManHashOne( int iLit0, int iLit1, int iLitC, int TableSize ) 
{
    unsigned Key = iLitC * 2011;
    Key += Abc_Lit2Var(iLit0) * 7937;
    Key += Abc_Lit2Var(iLit1) * 2971;
    Key += Abc_LitIsCompl(iLit0) * 911;
    Key += Abc_LitIsCompl(iLit1) * 353;
    return (int)(Key % TableSize);
}
static inline int * Gia_ManHashFind( Gia_Man_t * p, int iLit0, int iLit1, int iLitC )
{
    int iThis, * pPlace = Vec_IntEntryP( &p->vHTable, Gia_ManHashOne( iLit0, iLit1, iLitC, Vec_IntSize(&p->vHTable) ) );
    assert( Vec_IntSize(&p->vHash) == Gia_ManObjNum(p) );
    assert( p->pMuxes || iLit0 < iLit1 );
    assert( iLit0 < iLit1 || (!Abc_LitIsCompl(iLit0) && !Abc_LitIsCompl(iLit1)) );
    assert( iLitC == -1 || !Abc_LitIsCompl(iLit1) );
    for ( ; (iThis = *pPlace); pPlace = Vec_IntEntryP(&p->vHash, iThis) )
    {
        Gia_Obj_t * pThis = Gia_ManObj( p, iThis );
        if ( Gia_ObjFaninLit0(pThis, iThis) == iLit0 && Gia_ObjFaninLit1(pThis, iThis) == iLit1 && (p->pMuxes == NULL || Gia_ObjFaninLit2p(p, pThis) == iLitC) )
            break;
    }
    return pPlace;
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Gia_ManHashLookupInt( Gia_Man_t * p, int iLit0, int iLit1 )
{
    if ( iLit0 > iLit1 )
        iLit0 ^= iLit1, iLit1 ^= iLit0, iLit0 ^= iLit1;
    return Abc_Var2Lit( *Gia_ManHashFind( p, iLit0, iLit1, -1 ), 0 );
}
int Gia_ManHashLookup( Gia_Man_t * p, Gia_Obj_t * p0, Gia_Obj_t * p1 )
{
    int iLit0 = Gia_ObjToLit( p, p0 );
    int iLit1 = Gia_ObjToLit( p, p1 );
    return Gia_ManHashLookupInt( p, iLit0, iLit1 );
}

/**Function*************************************************************

  Synopsis    [Starts the hash table.]

  Description [Creates the two vectors and leaves them holding no entry. The
  bucket array p->vHTable is filled with zeros, its length taken from
  Abc_PrimeCudd, which returns the next prime at or above its argument: the
  current AND count plus 1000, or p->nObjsAlloc when the graph holds no AND
  yet. Gia_ManAndNum in gia.h derives that count by subtracting the two
  combinational-I/O lists and the constant from p->nObjs rather than storing
  it, so the sizing follows the graph as it stands. The link array p->vHash is
  grown to the larger of the bucket count and the object count and then filled
  with one zero per existing object, and that fill is what establishes the
  one-slot-per-object layout Gia_ManHashFind asserts. The entry assertion
  records that this runs only on a manager whose table is not already live.
  What it does not do is insert anything: an AND that already exists stays
  unfindable, and a lookup will report a miss for it, until something hashes it
  again. Gia_ManHashStart is the variant that also populates.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManHashAlloc( Gia_Man_t * p )  
{
    assert( Vec_IntSize(&p->vHTable) == 0 );
    Vec_IntFill( &p->vHTable, Abc_PrimeCudd( Gia_ManAndNum(p) ? Gia_ManAndNum(p) + 1000 : p->nObjsAlloc ), 0 );
    Vec_IntGrow( &p->vHash, Abc_MaxInt(Vec_IntSize(&p->vHTable), Gia_ManObjNum(p)) );
    Vec_IntFill( &p->vHash, Gia_ManObjNum(p), 0 );
//printf( "Alloced table with %d entries.\n", Vec_IntSize(&p->vHTable) );
}

/**Function*************************************************************

  Synopsis    [Starts the hash table.]

  Description [Calls Gia_ManHashAlloc for the two vectors and then walks
  Gia_ManForEachAnd inserting every AND the graph already holds, so the table
  describes the graph as it stands and a lookup can find a node that was built
  before the table existed. That population step is the entire difference from
  Gia_ManHashAlloc, and it is what makes this the entry point to use on a
  manager that is not empty. Each insertion asserts that the slot it lands on
  is still zero, which records the expectation that no two ANDs in the graph
  are structurally identical at this moment; a graph that does hold such a pair
  stops on that assertion rather than quietly dropping one of them from the
  table. The filter is worth reading carefully: Gia_ManForEachAnd selects on
  Gia_ObjIsAnd in gia.h, which tests only that the object is not a terminal and
  carries a real offset, so it admits buffers and real XORs alongside plain
  ANDs rather than plain ANDs alone.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManHashStart( Gia_Man_t * p )  
{
    Gia_Obj_t * pObj;
    int * pPlace, i;
    Gia_ManHashAlloc( p );
    Gia_ManForEachAnd( p, pObj, i )
    {
        pPlace = Gia_ManHashFind( p, Gia_ObjFaninLit0(pObj, i), Gia_ObjFaninLit1(pObj, i), Gia_ObjFaninLit2(p, i) );
        assert( *pPlace == 0 );
        *pPlace = i;
    }
}

/**Function*************************************************************

  Synopsis    [Stops the hash table.]

  Description [Releases the storage behind both vectors. Vec_IntErase is the
  right call rather than Vec_IntFree because vHash and vHTable are embedded in
  Gia_Man_t by value, not held by pointer: it frees the element array and zeros
  the size and capacity, leaving the two Vec_Int_t members themselves in place
  and reusable. Because ABC_FREE also nulls what it frees, a second call is
  harmless. The state left behind is load bearing elsewhere: a zero-size
  vHTable is the test the rest of the package reads as "no table is live", so
  Gia_ManAppendObj in gia.h stops pushing a vHash entry per new object once
  this has run, and Gia_ManHashAnd asserts exactly that emptiness on its
  p->fGiaSimple path. Lookups are not merely slower afterwards, they are
  invalid, since Gia_ManHashFind indexes both vectors and asserts the
  one-entry-per-object layout this call discards.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManHashStop( Gia_Man_t * p )  
{
    Vec_IntErase( &p->vHTable );
    Vec_IntErase( &p->vHash );
}

/**Function*************************************************************

  Synopsis    [Resizes the hash table.]

  Description [Re-buckets the table into Abc_PrimeCudd( 2 * Gia_ManAndNum(p) )
  entries, roughly doubling the bucket count to the next prime at or above it.
  The old bucket array is kept as a local, a fresh zeroed array replaces it in
  the manager, and every chain of the old array is then walked entry by entry.
  Each entry's next link is cleared in p->vHash before Gia_ManHashFind is asked
  where it now belongs, so the same vHash slots are reused in place and only the
  bucket array is reallocated; the next link is read into iNext before that
  clear, which is what lets the walk continue through a chain it dismantles. The
  function then checks its own work, counting what it moved and asserting that
  the count equals Gia_ManAndNum(p) - Gia_ManBufNum(p) - the AND count with the
  buffers taken off, buffers not being hashed - so a table that has drifted out
  of step with the graph is caught here rather than later. The old array is
  erased last. Callers never size the table themselves. Gia_ManHashAnd and
  Gia_ManHashXorReal are the two functions that ask for growth, and both guard
  the call the same way, requiring ( p->nObjs & 0xFF ) == 0 as well as twice the
  bucket count being below the AND count: the test is reached only on every
  256th object, so the table is allowed to run up towards two entries per bucket
  in between rather than being resized as soon as it could be.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManHashResize( Gia_Man_t * p )
{
    int i, iThis, iNext, Counter, Counter2, * pPlace;
    Vec_Int_t vOld = p->vHTable;
    assert( Vec_IntSize(&vOld) > 0 );
    // replace the table
    Vec_IntZero( &p->vHTable );
    Vec_IntFill( &p->vHTable, Abc_PrimeCudd( 2 * Gia_ManAndNum(p) ), 0 ); 
    // rehash the entries from the old table
    Counter = 0;
    Vec_IntForEachEntry( &vOld, iThis, i )
        for ( iNext = Vec_IntEntry(&p->vHash, iThis);  
              iThis;  iThis = iNext,   
              iNext = Vec_IntEntry(&p->vHash, iThis)  )
        {
            Gia_Obj_t * pThis0 = Gia_ManObj( p, iThis );
            Vec_IntWriteEntry( &p->vHash, iThis, 0 );
            pPlace = Gia_ManHashFind( p, Gia_ObjFaninLit0(pThis0, iThis), Gia_ObjFaninLit1(pThis0, iThis), Gia_ObjFaninLit2p(p, pThis0) );
            assert( *pPlace == 0 ); // should not be there
            *pPlace = iThis;
            assert( *pPlace != 0 );
            Counter++;
        }
    Counter2 = Gia_ManAndNum(p) - Gia_ManBufNum(p);
    assert( Counter == Counter2 );
//    if ( p->fVerbose )
//        printf( "Resizing GIA hash table: %d -> %d.\n", Vec_IntSize(&vOld), Vec_IntSize(&p->vHTable) );
    Vec_IntErase( &vOld );
}

/**Function********************************************************************

  Synopsis    [Profiles the hash table.]

  Description []

  SideEffects []

  SeeAlso     []

******************************************************************************/
void Gia_ManHashProfile( Gia_Man_t * p )
{
    int iEntry;
    int i, Counter, Limit;
    printf( "Table size = %d. Entries = %d. ", Vec_IntSize(&p->vHTable), Gia_ManAndNum(p) );
    printf( "Hits = %d. Misses = %d.\n", (int)p->nHashHit, (int)p->nHashMiss );
    Limit = Abc_MinInt( 1000, Vec_IntSize(&p->vHTable) );
    for ( i = 0; i < Limit; i++ )
    {
        Counter = 0;
        for ( iEntry = Vec_IntEntry(&p->vHTable, i); 
              iEntry; 
              iEntry = iEntry? Vec_IntEntry(&p->vHash, iEntry) : 0 )
            Counter++;
        if ( Counter ) 
            printf( "%d ", Counter );
    }
    printf( "\n" );
}

/**Function*************************************************************

  Synopsis    [Recognizes what nodes are control and data inputs of a MUX.]

  Description [If the node is a MUX, returns the control variable C.
  Assigns nodes T and E to be the then and else variables of the MUX. 
  Node C is never complemented. Nodes T and E can be complemented.
  This function also recognizes EXOR/NEXOR gates as MUXes.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
static inline Gia_Obj_t * Gia_ObjRecognizeMuxTwo( Gia_Obj_t * pNode0, Gia_Obj_t * pNode1, Gia_Obj_t ** ppNodeT, Gia_Obj_t ** ppNodeE )
{
    assert( !Gia_IsComplement(pNode0) );
    assert( !Gia_IsComplement(pNode1) );
    // find the control variable
    if ( Gia_ObjFanin1(pNode0) == Gia_ObjFanin1(pNode1) && (Gia_ObjFaninC1(pNode0) ^ Gia_ObjFaninC1(pNode1)) )
    {
//        if ( FrGia_IsComplement(pNode1->p2) )
        if ( Gia_ObjFaninC1(pNode0) )
        { // pNode2->p2 is positive phase of C
            *ppNodeT = Gia_Not(Gia_ObjChild0(pNode1));//pNode2->p1);
            *ppNodeE = Gia_Not(Gia_ObjChild0(pNode0));//pNode1->p1);
            return Gia_ObjChild1(pNode1);//pNode2->p2;
        }
        else
        { // pNode1->p2 is positive phase of C
            *ppNodeT = Gia_Not(Gia_ObjChild0(pNode0));//pNode1->p1);
            *ppNodeE = Gia_Not(Gia_ObjChild0(pNode1));//pNode2->p1);
            return Gia_ObjChild1(pNode0);//pNode1->p2;
        }
    }
    else if ( Gia_ObjFanin0(pNode0) == Gia_ObjFanin0(pNode1) && (Gia_ObjFaninC0(pNode0) ^ Gia_ObjFaninC0(pNode1)) )
    {
//        if ( FrGia_IsComplement(pNode1->p1) )
        if ( Gia_ObjFaninC0(pNode0) )
        { // pNode2->p1 is positive phase of C
            *ppNodeT = Gia_Not(Gia_ObjChild1(pNode1));//pNode2->p2);
            *ppNodeE = Gia_Not(Gia_ObjChild1(pNode0));//pNode1->p2);
            return Gia_ObjChild0(pNode1);//pNode2->p1;
        }
        else
        { // pNode1->p1 is positive phase of C
            *ppNodeT = Gia_Not(Gia_ObjChild1(pNode0));//pNode1->p2);
            *ppNodeE = Gia_Not(Gia_ObjChild1(pNode1));//pNode2->p2);
            return Gia_ObjChild0(pNode0);//pNode1->p1;
        }
    }
    else if ( Gia_ObjFanin0(pNode0) == Gia_ObjFanin1(pNode1) && (Gia_ObjFaninC0(pNode0) ^ Gia_ObjFaninC1(pNode1)) )
    {
//        if ( FrGia_IsComplement(pNode1->p1) )
        if ( Gia_ObjFaninC0(pNode0) )
        { // pNode2->p2 is positive phase of C
            *ppNodeT = Gia_Not(Gia_ObjChild0(pNode1));//pNode2->p1);
            *ppNodeE = Gia_Not(Gia_ObjChild1(pNode0));//pNode1->p2);
            return Gia_ObjChild1(pNode1);//pNode2->p2;
        }
        else
        { // pNode1->p1 is positive phase of C
            *ppNodeT = Gia_Not(Gia_ObjChild1(pNode0));//pNode1->p2);
            *ppNodeE = Gia_Not(Gia_ObjChild0(pNode1));//pNode2->p1);
            return Gia_ObjChild0(pNode0);//pNode1->p1;
        }
    }
    else if ( Gia_ObjFanin1(pNode0) == Gia_ObjFanin0(pNode1) && (Gia_ObjFaninC1(pNode0) ^ Gia_ObjFaninC0(pNode1)) )
    {
//        if ( FrGia_IsComplement(pNode1->p2) )
        if ( Gia_ObjFaninC1(pNode0) )
        { // pNode2->p1 is positive phase of C
            *ppNodeT = Gia_Not(Gia_ObjChild1(pNode1));//pNode2->p2);
            *ppNodeE = Gia_Not(Gia_ObjChild0(pNode0));//pNode1->p1);
            return Gia_ObjChild0(pNode1);//pNode2->p1;
        }
        else
        { // pNode1->p2 is positive phase of C
            *ppNodeT = Gia_Not(Gia_ObjChild0(pNode0));//pNode1->p1);
            *ppNodeE = Gia_Not(Gia_ObjChild1(pNode1));//pNode2->p2);
            return Gia_ObjChild1(pNode0);//pNode1->p2;
        }
    }
    assert( 0 ); // this is not MUX
    return NULL;
}


/**Function*************************************************************

  Synopsis    [Rehashes AIG with mapping.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
static inline Gia_Obj_t * Gia_ManHashAndP( Gia_Man_t * p, Gia_Obj_t * p0, Gia_Obj_t * p1 )  
{ 
    return Gia_ObjFromLit( p, Gia_ManHashAnd( p, Gia_ObjToLit(p, p0), Gia_ObjToLit(p, p1) ) );
}

/**Function*************************************************************

  Synopsis    [Rehashes AIG with mapping.]

  Description [http://fmv.jku.at/papers/BrummayerBiere-MEMICS06.pdf]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
static inline Gia_Obj_t * Gia_ManAddStrash( Gia_Man_t * p, Gia_Obj_t * p0, Gia_Obj_t * p1 )  
{ 
    Gia_Obj_t * pNode0, * pNode1, * pFanA, * pFanB, * pFanC, * pFanD;
    assert( p->fAddStrash );
    pNode0 = Gia_Regular(p0);
    pNode1 = Gia_Regular(p1);
    if ( !Gia_ObjIsAnd(pNode0) && !Gia_ObjIsAnd(pNode1) )
        return NULL;
    pFanA = Gia_ObjIsAnd(pNode0) ? Gia_ObjChild0(pNode0) : NULL;
    pFanB = Gia_ObjIsAnd(pNode0) ? Gia_ObjChild1(pNode0) : NULL;
    pFanC = Gia_ObjIsAnd(pNode1) ? Gia_ObjChild0(pNode1) : NULL;
    pFanD = Gia_ObjIsAnd(pNode1) ? Gia_ObjChild1(pNode1) : NULL;
    if ( Gia_IsComplement(p0) )
    {
        if ( pFanA == Gia_Not(p1) || pFanB == Gia_Not(p1) )
            return p1;
        if ( pFanB == p1 )
            return Gia_ManHashAndP( p, Gia_Not(pFanA), pFanB );
        if ( pFanA == p1 )
            return Gia_ManHashAndP( p, Gia_Not(pFanB), pFanA );
    }
    else
    {
        if ( pFanA == Gia_Not(p1) || pFanB == Gia_Not(p1) )
            return Gia_ManConst0(p);
        if ( pFanA == p1 || pFanB == p1 )
            return p0;
    }
    if ( Gia_IsComplement(p1) )
    {
        if ( pFanC == Gia_Not(p0) || pFanD == Gia_Not(p0) )
            return p0;
        if ( pFanD == p0 )
            return Gia_ManHashAndP( p, Gia_Not(pFanC), pFanD );
        if ( pFanC == p0 )
            return Gia_ManHashAndP( p, Gia_Not(pFanD), pFanC );
    }
    else
    {
        if ( pFanC == Gia_Not(p0) || pFanD == Gia_Not(p0) )
            return Gia_ManConst0(p);
        if ( pFanC == p0 || pFanD == p0 )
            return p1;
    }
    if ( !Gia_IsComplement(p0) && !Gia_IsComplement(p1) ) 
    {
        if ( pFanA == Gia_Not(pFanC) || pFanA == Gia_Not(pFanD) || pFanB == Gia_Not(pFanC) || pFanB == Gia_Not(pFanD) )
            return Gia_ManConst0(p);
        if ( pFanA == pFanC || pFanB == pFanC )
            return Gia_ManHashAndP( p, p0, pFanD );
        if ( pFanB == pFanC || pFanB == pFanD )
            return Gia_ManHashAndP( p, pFanA, p1 );
        if ( pFanA == pFanD || pFanB == pFanD )
            return Gia_ManHashAndP( p, p0, pFanC );
        if ( pFanA == pFanC || pFanA == pFanD )
            return Gia_ManHashAndP( p, pFanB, p1 );
    }
    else if ( Gia_IsComplement(p0) && !Gia_IsComplement(p1) )
    {
        if ( pFanA == Gia_Not(pFanC) || pFanA == Gia_Not(pFanD) || pFanB == Gia_Not(pFanC) || pFanB == Gia_Not(pFanD) )
            return p1;
        if ( pFanB == pFanC || pFanB == pFanD )
            return Gia_ManHashAndP( p, Gia_Not(pFanA), p1 );
        if ( pFanA == pFanC || pFanA == pFanD )
            return Gia_ManHashAndP( p, Gia_Not(pFanB), p1 );
    }
    else if ( !Gia_IsComplement(p0) && Gia_IsComplement(p1) )
    {
        if ( pFanC == Gia_Not(pFanA) || pFanC == Gia_Not(pFanB) || pFanD == Gia_Not(pFanA) || pFanD == Gia_Not(pFanB) )
            return p0;
        if ( pFanD == pFanA || pFanD == pFanB )
            return Gia_ManHashAndP( p, Gia_Not(pFanC), p0 );
        if ( pFanC == pFanA || pFanC == pFanB )
            return Gia_ManHashAndP( p, Gia_Not(pFanD), p0 );
    }
    else // if ( Gia_IsComplement(p0) && Gia_IsComplement(p1) )
    {
        if ( pFanA == pFanD && pFanB == Gia_Not(pFanC) )
            return Gia_Not(pFanA);
        if ( pFanB == pFanC && pFanA == Gia_Not(pFanD) )
            return Gia_Not(pFanB);
        if ( pFanA == pFanC && pFanB == Gia_Not(pFanD) )
            return Gia_Not(pFanA);
        if ( pFanB == pFanD && pFanA == Gia_Not(pFanC) )
            return Gia_Not(pFanB);
    }
/*
    if ( !Gia_IsComplement(p0) || !Gia_IsComplement(p1) )
        return NULL;
    if ( !Gia_ObjIsAnd(pNode0) || !Gia_ObjIsAnd(pNode1) )
        return NULL;
    if ( (Gia_ObjFanin0(pNode0) == Gia_ObjFanin0(pNode1) && (Gia_ObjFaninC0(pNode0) ^ Gia_ObjFaninC0(pNode1))) || 
         (Gia_ObjFanin0(pNode0) == Gia_ObjFanin1(pNode1) && (Gia_ObjFaninC0(pNode0) ^ Gia_ObjFaninC1(pNode1))) ||
         (Gia_ObjFanin1(pNode0) == Gia_ObjFanin0(pNode1) && (Gia_ObjFaninC1(pNode0) ^ Gia_ObjFaninC0(pNode1))) ||
         (Gia_ObjFanin1(pNode0) == Gia_ObjFanin1(pNode1) && (Gia_ObjFaninC1(pNode0) ^ Gia_ObjFaninC1(pNode1))) )
    {
        Gia_Obj_t * pNodeC, * pNodeT, * pNodeE;
        int fCompl;
        pNodeC = Gia_ObjRecognizeMuxTwo( pNode0, pNode1, &pNodeT, &pNodeE );
        // using non-standard canonical rule for MUX (d0 is not compl; d1 may be compl)
        if ( (fCompl = Gia_IsComplement(pNodeE)) )
        {
            pNodeE = Gia_Not(pNodeE);
            pNodeT = Gia_Not(pNodeT);
        }
        pNode0 = Gia_ManHashAndP( p, Gia_Not(pNodeC), pNodeE );
        pNode1 = Gia_ManHashAndP( p, pNodeC,          pNodeT );
        p->fAddStrash = 0;
        pNodeC = Gia_NotCond( Gia_ManHashAndP( p, Gia_Not(pNode0), Gia_Not(pNode1) ), !fCompl );
        p->fAddStrash = 1;
        return pNodeC;
    }
*/
    return NULL;
}

/**Function*************************************************************

  Synopsis    [Hashes XOR gate.]

  Description [Finds or creates the real XOR - one object that carries the
  operator, as against the several AND objects Gia_ManHashXor builds - and
  returns its literal. The entry assertion requires p->fAddStrash to be clear:
  the additive strashing path has no XOR case, so the two are mutually
  exclusive. Four degenerate cases fold first, a constant fanin passing the
  other fanin through with or without inversion, a literal against itself
  giving 0 and against its own complement giving 1. The growth check then runs,
  on the same every-256th-object guard Gia_ManHashAnd uses. Canonical form
  differs from the AND case in both of its parts, and the difference is easy to
  misread. The swap runs when iLit0 < iLit1, the opposite direction from
  Gia_ManHashAnd, so a stored real XOR carries its fanin literals descending at
  this layer. That descending order carries a precondition which is enforced but
  not stated here: Gia_ManHashFind permits a descending pair only when p->pMuxes
  is allocated, so this constructor is reachable only on a manager that has that
  side table, even though no MUX is involved and Gia_ManAppendXorReal never
  touches it. Callers in the tree allocate it right after Gia_ManStart, before
  any hashing - giaMuxes.c and giaBalAig.c both do. Both complements are then
  stripped from the fanins and accumulated in fCompl, which is re-applied to the
  literal handed back. Stripping is sound because XOR is linear in each input -
  inverting an input only inverts the output - and its effect is that the four
  inversion patterns over one pair of variables share a single stored object,
  with the caller's inversion carried on the returned literal instead of in the
  graph. Note that the comparison here is on literals; Gia_ManAppendXorReal in
  gia.h, which does the writing, orders its own fanins by variable, so a
  statement about one layer does not carry to the other. That append stores
  iDiff0 < iDiff1, and it is that ordering, not any type field, that later makes
  Gia_ObjIsXor report the object as a real XOR. Insertion goes through the same
  interior-slot capacity guard described under Gia_ManHashAnd.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Gia_ManHashXorReal( Gia_Man_t * p, int iLit0, int iLit1 )  
{ 
    int fCompl = 0;
    assert( p->fAddStrash == 0 );
    if ( iLit0 < 2 )
        return iLit0 ? Abc_LitNot(iLit1) : iLit1;
    if ( iLit1 < 2 )
        return iLit1 ? Abc_LitNot(iLit0) : iLit0;
    if ( iLit0 == iLit1 )
        return 0;
    if ( iLit0 == Abc_LitNot(iLit1) )
        return 1;
    if ( (p->nObjs & 0xFF) == 0 && 2 * Vec_IntSize(&p->vHTable) < Gia_ManAndNum(p) )
        Gia_ManHashResize( p );
    if ( iLit0 < iLit1 )
        iLit0 ^= iLit1, iLit1 ^= iLit0, iLit0 ^= iLit1;
    if ( Abc_LitIsCompl(iLit0) )
        iLit0 = Abc_LitNot(iLit0), fCompl ^= 1;
    if ( Abc_LitIsCompl(iLit1) )
        iLit1 = Abc_LitNot(iLit1), fCompl ^= 1;
    {
        int *pPlace = Gia_ManHashFind( p, iLit0, iLit1, -1 );
        if ( *pPlace )
        {
            p->nHashHit++;
            return Abc_Var2Lit( *pPlace, fCompl );
        }
        p->nHashMiss++;
        if ( Vec_IntSize(&p->vHash) < Vec_IntCap(&p->vHash) )
            *pPlace = Abc_Lit2Var( Gia_ManAppendXorReal( p, iLit0, iLit1 ) );
        else
        {
            int iNode = Gia_ManAppendXorReal( p, iLit0, iLit1 );
            pPlace = Gia_ManHashFind( p, iLit0, iLit1, -1 );
            assert( *pPlace == 0 );
            *pPlace = Abc_Lit2Var( iNode );
        }
        return Abc_Var2Lit( *pPlace, fCompl );
    }
}

/**Function*************************************************************

  Synopsis    [Hashes MUX gate.]

  Description [Finds or creates the real MUX - one object that carries the
  operator, as against the AND objects Gia_ManHashMux builds - and returns its
  literal. As with Gia_ManHashXorReal the entry assertion requires p->fAddStrash
  to be clear. A sequence of degenerate cases is folded first, and most of them
  leave this operator altogether rather than returning a constant: a constant
  control selects one data literal outright, a constant data literal turns the
  MUX into a Gia_ManHashAnd or a Gia_ManHashOr, equal data literals collapse to
  one, a control equal to a data literal or to the complement of the other
  collapses to an AND or an OR, and two data literals over the same variable
  become a Gia_ManHashXorReal. So a call here does not guarantee that a MUX
  object is what gets built. The manager must already carry the pMuxes side
  table, which Gia_ManAppendMuxReal asserts outright and which callers allocate
  right after Gia_ManStart. Canonical form orders the two data literals with a
  swap when iLit0 > iLit1, and that swap negates the control literal in the same
  statement, because exchanging the then and else inputs of a multiplexer
  reverses the sense of its select - the two edits are one transformation and
  neither is valid alone. A complemented second data literal is then pushed out
  of both data literals into fCompl and re-applied to the literal handed back.
  The comparison here is on literals; Gia_ManAppendMuxReal in gia.h orders by
  variable, so the two layers are again not interchangeable on this point. That
  append keeps the control literal in the manager's pMuxes side table and
  complements it in the branch where the data order is reversed, mirroring the
  swap done here. The offset and complement fields it leaves in the object carry
  iDiff0 greater than iDiff1, which is the same ordering a real AND carries: a
  real MUX is not distinguishable from a real AND by the object alone, only by
  the presence of its pMuxes entry, which is why Gia_ObjIsAndReal has to consult
  Gia_ObjIsMux to tell the two apart. Insertion goes through the same
  interior-slot capacity guard described under Gia_ManHashAnd.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Gia_ManHashMuxReal( Gia_Man_t * p, int iLitC, int iLit1, int iLit0 )  
{
    int fCompl = 0;
    assert( p->fAddStrash == 0 );
    if ( iLitC < 2 )
        return iLitC ? iLit1 : iLit0;
    if ( iLit0 < 2 )
        return iLit0 ? Gia_ManHashOr(p, Abc_LitNot(iLitC), iLit1) : Gia_ManHashAnd(p, iLitC, iLit1);
    if ( iLit1 < 2 )
        return iLit1 ? Gia_ManHashOr(p, iLitC, iLit0) : Gia_ManHashAnd(p, Abc_LitNot(iLitC), iLit0);
    assert( iLit0 > 1 && iLit1 > 1 && iLitC > 1 );
    if ( iLit0 == iLit1 )
        return iLit0;
    if ( iLitC == iLit0 || iLitC == Abc_LitNot(iLit1) )
        return Gia_ManHashAnd(p, iLit0, iLit1);
    if ( iLitC == iLit1 || iLitC == Abc_LitNot(iLit0) )
        return Gia_ManHashOr(p, iLit0, iLit1);
    if ( Abc_Lit2Var(iLit0) == Abc_Lit2Var(iLit1) )
        return Gia_ManHashXorReal( p, iLitC, iLit0 );
    if ( iLit0 > iLit1 )
        iLit0 ^= iLit1, iLit1 ^= iLit0, iLit0 ^= iLit1, iLitC = Abc_LitNot(iLitC);
    if ( Abc_LitIsCompl(iLit1) )
        iLit0 = Abc_LitNot(iLit0), iLit1 = Abc_LitNot(iLit1), fCompl = 1;
    {
        int *pPlace = Gia_ManHashFind( p, iLit0, iLit1, iLitC );
        if ( *pPlace )
        {
            p->nHashHit++;
            return Abc_Var2Lit( *pPlace, fCompl );
        }
        p->nHashMiss++;
        if ( Vec_IntSize(&p->vHash) < Vec_IntCap(&p->vHash) )
            *pPlace = Abc_Lit2Var( Gia_ManAppendMuxReal( p, iLitC, iLit1, iLit0 ) );
        else
        {
            int iNode = Gia_ManAppendMuxReal( p, iLitC, iLit1, iLit0 );
            pPlace = Gia_ManHashFind( p, iLit0, iLit1, iLitC );
            assert( *pPlace == 0 );
            *pPlace = Abc_Lit2Var( iNode );
        }
        return Abc_Var2Lit( *pPlace, fCompl );
    }
}

/**Function*************************************************************

  Synopsis    [Hashes AND gate.]

  Description [The constructor most graph building goes through, and the order
  of its steps is part of its contract rather than an implementation detail.
  Four constant cases fold first, and they fold before any manager flag is
  consulted: a constant fanin returns the other fanin or 0, two equal fanins
  return that fanin, and two complementary fanins return 0. The consequence is
  that folding happens even under p->fGiaSimple, which is not obvious from
  reading that flag's other effects. p->fGiaSimple is checked next; it asserts
  that no table is live and appends unconditionally, so it is the raw path with
  no lookup and no deduplication. Otherwise the growth check runs, then
  p->fAddStrash gets its chance to rewrite the pair through Gia_ManAddStrash,
  whose result is returned as a literal when it produced an object. Only after
  all of that is canonical order applied - a swap when iLit0 > iLit1, comparing
  literals at this layer - and Gia_ManHashFind consulted. A hit counts into
  p->nHashHit and returns the existing object's literal; a miss counts into
  p->nHashMiss and creates the object through Gia_ManAppendAnd in gia.h, whose
  iDiff0 not less than iDiff1 ordering is what afterwards separates a plain AND
  or buffer from a real XOR, there being no type field to consult. Two
  properties matter to callers. The return value is always a literal and never
  an object identifier, and it may name an object that already existed, an
  argument, or a constant, so the call carries no implication that a new object
  was made. And the interior-slot hazard is handled here: the int pointer
  Gia_ManHashFind returns points inside p->vHash or p->vHTable, and appending an
  object pushes onto p->vHash, which can reallocate it. The code writes through
  that pointer only while the vector still has spare capacity, and on the
  capacity-exhausting path appends first and re-runs the lookup for a fresh
  slot. That is a distinct hazard from the object-array reallocation in
  Gia_ManAppendObj, which invalidates a Gia_Obj_t pointer rather than an int
  pointer, and the same guard appears in Gia_ManHashXorReal and
  Gia_ManHashMuxReal. Gia_ManHashOr below shares this banner: it is the De
  Morgan wrapper, complementing both arguments, calling this function and
  complementing the result, so an OR is stored as an AND and no separate OR
  object kind exists.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Gia_ManHashAnd( Gia_Man_t * p, int iLit0, int iLit1 )  
{ 
    if ( iLit0 < 2 )
        return iLit0 ? iLit1 : 0;
    if ( iLit1 < 2 )
        return iLit1 ? iLit0 : 0;
    if ( iLit0 == iLit1 )
        return iLit1;
    if ( iLit0 == Abc_LitNot(iLit1) )
        return 0;
    if ( p->fGiaSimple )
    {
        assert( Vec_IntSize(&p->vHTable) == 0 );
        return Gia_ManAppendAnd( p, iLit0, iLit1 );
    }
    if ( (p->nObjs & 0xFF) == 0 && 2 * Vec_IntSize(&p->vHTable) < Gia_ManAndNum(p) )
        Gia_ManHashResize( p );
    if ( p->fAddStrash )
    {
        Gia_Obj_t * pObj = Gia_ManAddStrash( p, Gia_ObjFromLit(p, iLit0), Gia_ObjFromLit(p, iLit1) );
        if ( pObj != NULL )
            return Gia_ObjToLit( p, pObj );
    }
    if ( iLit0 > iLit1 )
        iLit0 ^= iLit1, iLit1 ^= iLit0, iLit0 ^= iLit1;
    {
        int * pPlace = Gia_ManHashFind( p, iLit0, iLit1, -1 );
        if ( *pPlace )
        {
            p->nHashHit++;
            return Abc_Var2Lit( *pPlace, 0 );
        }
        p->nHashMiss++;
        if ( Vec_IntSize(&p->vHash) < Vec_IntCap(&p->vHash) )
            *pPlace = Abc_Lit2Var( Gia_ManAppendAnd( p, iLit0, iLit1 ) );
        else
        {
            int iNode = Gia_ManAppendAnd( p, iLit0, iLit1 );
            pPlace = Gia_ManHashFind( p, iLit0, iLit1, -1 );
            assert( *pPlace == 0 );
            *pPlace = Abc_Lit2Var( iNode );
        }
        return Abc_Var2Lit( *pPlace, 0 );
    }
}
int Gia_ManHashOr( Gia_Man_t * p, int iLit0, int iLit1 )  
{ 
    return Abc_LitNot(Gia_ManHashAnd( p, Abc_LitNot(iLit0), Abc_LitNot(iLit1) ));
}

/**Function*************************************************************

  Synopsis    []

  Description [The query form of Gia_ManHashAnd. It folds the same four constant
  cases and applies the same canonical swap, then looks the pair up and returns
  -1 when the chain holds no match. That -1 is the whole point of the function
  and is what separates it from Gia_ManHashAnd: nothing is appended and the
  table is never resized, so the graph is left exactly as it was found and a
  caller may ask the question without committing to the answer. The -1 is not a
  literal. Literals 0 and 1 are the two constants and every other literal is a
  non-negative object identifier shifted left with its inversion bit, so a
  caller that forwards the result without testing for -1 feeds a negative value
  into literal arithmetic rather than getting a harmless wrong node. Folding
  still runs ahead of the lookup, so a call whose arguments fold comes back with
  a constant or one of its arguments even though no chain was searched, and only
  a genuine lookup miss produces -1.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Gia_ManHashAndTry( Gia_Man_t * p, int iLit0, int iLit1 )  
{ 
    if ( iLit0 < 2 )
        return iLit0 ? iLit1 : 0;
    if ( iLit1 < 2 )
        return iLit1 ? iLit0 : 0;
    if ( iLit0 == iLit1 )
        return iLit1;
    if ( iLit0 == Abc_LitNot(iLit1) )
        return 0;
    if ( iLit0 > iLit1 )
        iLit0 ^= iLit1, iLit1 ^= iLit0, iLit0 ^= iLit1;
    {
        int * pPlace = Gia_ManHashFind( p, iLit0, iLit1, -1 );
        if ( *pPlace ) 
            return Abc_Var2Lit( *pPlace, 0 );
        return -1;
    }
}

/**Function*************************************************************

  Synopsis    []

  Description [Builds the structural XOR: the function is expressed with several
  AND objects instead of the single object Gia_ManHashXorReal creates. Both
  branches here go through three AND constructions and nothing else, though how
  many objects actually appear depends on what those constructions fold or find
  already hashed. Under p->fGiaSimple the two half terms are combined with
  Gia_ManHashOr, which is itself an AND. Otherwise the inversion of each
  argument is stripped, their combined parity is remembered in fCompl, the two
  half terms are built from the regular literals so that the four inversion
  patterns over a variable pair share the same objects, and the parity is
  re-applied to the result through Abc_LitNotCond - which is how one subgraph
  serves both XOR and XNOR. What comes out is ordinary AND logic: no predicate
  reports any of it as an XOR afterwards and a later pass sees unremarkable
  ANDs, and that loss of the operator is the reason the real form exists beside
  this one. Nothing here reads or writes p->pMuxes, and the manager needs no XOR
  support of any kind to hold the result, which is what makes this form usable
  on any graph.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Gia_ManHashXor( Gia_Man_t * p, int iLit0, int iLit1 )  
{ 
    if ( p->fGiaSimple )
        return Gia_ManHashOr(p, Gia_ManHashAnd(p, iLit0, Abc_LitNot(iLit1)), Gia_ManHashAnd(p, Abc_LitNot(iLit0), iLit1) );
    else
    {
        int fCompl = Abc_LitIsCompl(iLit0) ^ Abc_LitIsCompl(iLit1);
        int iTemp0 = Gia_ManHashAnd( p, Abc_LitRegular(iLit0), Abc_LitNot(Abc_LitRegular(iLit1)) );
        int iTemp1 = Gia_ManHashAnd( p, Abc_LitRegular(iLit1), Abc_LitNot(Abc_LitRegular(iLit0)) );
        return Abc_LitNotCond( Gia_ManHashAnd( p, Abc_LitNot(iTemp0), Abc_LitNot(iTemp1) ), !fCompl );
    }
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Gia_ManHashMux( Gia_Man_t * p, int iCtrl, int iData1, int iData0 )  
{ 
    if ( p->fGiaSimple )
        return Gia_ManHashOr(p, Gia_ManHashAnd(p, iCtrl, iData1), Gia_ManHashAnd(p, Abc_LitNot(iCtrl), iData0) );
    else
    {
        int iTemp0, iTemp1, fCompl = 0;
        if ( iData0 > iData1 )
            iData0 ^= iData1, iData1 ^= iData0, iData0 ^= iData1, iCtrl = Abc_LitNot(iCtrl);
        if ( Abc_LitIsCompl(iData1) )
            iData0 = Abc_LitNot(iData0), iData1 = Abc_LitNot(iData1), fCompl = 1;
        iTemp0 = Gia_ManHashAnd( p, Abc_LitNot(iCtrl), iData0 );
        iTemp1 = Gia_ManHashAnd( p, iCtrl, iData1 );
        return Abc_LitNotCond( Gia_ManHashAnd( p, Abc_LitNot(iTemp0), Abc_LitNot(iTemp1) ), !fCompl );
    }
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Gia_ManHashMaj( Gia_Man_t * p, int iData0, int iData1, int iData2 )  
{ 
    int iTemp0 = Gia_ManHashOr( p, iData1, iData2 );
    int iTemp1 = Gia_ManHashAnd( p, iData0, iTemp0 );
    int iTemp2 = Gia_ManHashAnd( p, iData1, iData2 );
    return Gia_ManHashOr( p, iTemp1, iTemp2 );
}

/**Function*************************************************************

  Synopsis    [Rehashes AIG.]

  Description [Rebuilds a whole graph through a fresh hash table and returns a
  new manager. The argument is read only and is not freed, so the caller still
  owns it afterwards. A new manager is started at the old object count, the name
  and spec are copied, fAddStrash is taken from the parameter, the table is
  allocated, and the constant is seeded by writing literal 0 into its Value.
  Every object of the source is then visited in stored order and dispatched: an
  AND is rebuilt through Gia_ManHashAnd from the literals its two fanins were
  copied to, a combinational input is appended fresh, and a combinational output
  is appended over its copied driver - each storing the literal it produced back
  into the source object's Value, which is the copy-map protocol the whole
  rebuild rests on. Visiting in stored order is what makes the protocol work
  without recursion: fanin offsets point backwards, so both fanins of an object
  have already been rebuilt and already carry their new literals by the time the
  object is reached. The table is then stopped, fAddStrash cleared, and the
  register count carried over, and the result is passed through Gia_ManCleanup
  with the swap idiom, the new manager going in as pTemp and being stopped once
  its replacement is in hand. Two managers are therefore built and one freed on
  the way to the one returned, because Gia_ManCleanup itself duplicates rather
  than compacting in place. One property of the dispatch above is worth stating
  because it is silent: a buffer does not survive a rehash. The buffer branch is
  commented out, and Gia_ObjIsAnd is true for a buffer, so a buffer takes the
  AND branch; a buffer's two fanin offsets are equal, so both copied literals
  are the same literal, and Gia_ManHashAnd folds two equal arguments to that
  argument. The rebuilt graph simply has no buffers in it and nothing in the
  return value reports their removal.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Gia_Man_t * Gia_ManRehash( Gia_Man_t * p, int fAddStrash )  
{
    Gia_Man_t * pNew, * pTemp;
    Gia_Obj_t * pObj;
    int i;
    pNew = Gia_ManStart( Gia_ManObjNum(p) );
    pNew->pName = Abc_UtilStrsav( p->pName );
    pNew->pSpec = Abc_UtilStrsav( p->pSpec );
    pNew->fAddStrash = fAddStrash;
    Gia_ManHashAlloc( pNew );
    Gia_ManConst0(p)->Value = 0;
    Gia_ManForEachObj( p, pObj, i )
    {
        //if ( Gia_ObjIsBuf(pObj) )
        //    pObj->Value = Gia_ManAppendBuf( pNew, Gia_ObjFanin0Copy(pObj) );
        //else 
        if ( Gia_ObjIsAnd(pObj) )
            pObj->Value = Gia_ManHashAnd( pNew, Gia_ObjFanin0Copy(pObj), Gia_ObjFanin1Copy(pObj) );
        else if ( Gia_ObjIsCi(pObj) )
            pObj->Value = Gia_ManAppendCi( pNew );
        else if ( Gia_ObjIsCo(pObj) )
            pObj->Value = Gia_ManAppendCo( pNew, Gia_ObjFanin0Copy(pObj) );
    }
    Gia_ManHashStop( pNew );
    pNew->fAddStrash = 0;
    Gia_ManSetRegNum( pNew, Gia_ManRegNum(p) );
//    printf( "Top gate is %s\n", Gia_ObjFaninC0(Gia_ManCo(pNew, 0))? "OR" : "AND" );
    pNew = Gia_ManCleanup( pTemp = pNew );
    Gia_ManStop( pTemp );
    return pNew;
}


/**Function*************************************************************

  Synopsis    [Creates well-balanced AND gate.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Gia_ManHashAndMulti( Gia_Man_t * p, Vec_Int_t * vLits )
{
    if ( Vec_IntSize(vLits) == 0 )
        return 0;
    while ( Vec_IntSize(vLits) > 1 )
    {
        int i, k = 0, Lit1, Lit2, LitRes;
        Vec_IntForEachEntryDouble( vLits, Lit1, Lit2, i )
        {
            LitRes = Gia_ManHashAnd( p, Lit1, Lit2 );
            Vec_IntWriteEntry( vLits, k++, LitRes );
        }
        if ( Vec_IntSize(vLits) & 1 )
            Vec_IntWriteEntry( vLits, k++, Vec_IntEntryLast(vLits) );
        Vec_IntShrink( vLits, k );
    }
    assert( Vec_IntSize(vLits) == 1 );
    return Vec_IntEntry(vLits, 0);
}
int Gia_ManHashAndMulti2( Gia_Man_t * p, Vec_Int_t * vLits )
{
    int i, iLit, iRes = 1;
    Vec_IntForEachEntry( vLits, iLit, i )
        iRes = Gia_ManHashAnd( p, iRes, iLit );
    return iRes;
}
int Gia_ManHashDualMiter( Gia_Man_t * p, Vec_Int_t * vOuts )
{
    int i, iLit0, iLit1, iRes = 0;
    Vec_IntForEachEntryDouble( vOuts, iLit0, iLit1, i )
        iRes = Gia_ManHashOr( p, iRes, Gia_ManHashXor(p, iLit0, iLit1) );
    return iRes;
}



/**Function*************************************************************

  Synopsis    [Create multi-input tree.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int * Gia_ManCollectLiterals( int nVars )
{
    int i, * pRes = ABC_CALLOC( int, nVars );
    for ( i = 0; i < nVars; i++ )
        pRes[i] = Abc_Var2Lit( i+1, 0 );
    return pRes;
}
int * Gia_ManGenZero( int nBits )
{
    return ABC_CALLOC( int, nBits );
}
int * Gia_ManGenPerm( int nBits )
{
    int i, * pRes = ABC_CALLOC( int, nBits );
    srand( time(NULL) );
    for ( i = 0; i < nBits; i++ )
        pRes[i] = i;
    for ( i = 0; i < nBits; i++ )
    {
        int iPerm = rand() % nBits;
        ABC_SWAP( int, pRes[i], pRes[iPerm] );
    }
    return pRes;
}
int * Gia_ManGenPerm2( int nBits )
{
    int i, * pRes = ABC_CALLOC( int, nBits );
    srand( time(NULL) );
    for ( i = 0; i < nBits; i++ )
        pRes[i] = rand() % nBits;
    return pRes;
}
int Gia_ManMultiCheck( int * pPerm, int nPerm )
{
    int i;
    for ( i = 1; i < nPerm; i++ )
        if ( pPerm[i-1] <= pPerm[i] )
            return 0;
    return 1;
}
int Gia_ManMultiInputPerm( Gia_Man_t * pNew, int * pVars, int nVars, int * pPerm, int fOr, int fXor )
{
    int fPrint = 1;
    int i, iLit;
    if ( fPrint )
    {
        for ( i = 0; i < nVars; i++ )
            printf( "%d ", pPerm[i] );
        printf( "\n" );
    }
    while ( 1 )
    {
        for ( i = 1; i < nVars; i++ )
            if ( pPerm[i-1] >= pPerm[i] )
                break;
        if ( i == nVars )
            break;
        assert( pPerm[i-1] >= pPerm[i] );
        if ( pPerm[i-1] > pPerm[i] )
        {
            ABC_SWAP( int, pPerm[i-1], pPerm[i] );
            ABC_SWAP( int, pVars[i-1], pVars[i] );
        }
        else
        {
            assert( pPerm[i-1] == pPerm[i] );
            pPerm[i-1]++;
            if ( fXor )
                pVars[i-1] = Gia_ManHashXor( pNew, pVars[i-1], pVars[i] );
            else if ( fOr )
                pVars[i-1] = Gia_ManHashOr( pNew, pVars[i-1], pVars[i] );
            else
                pVars[i-1] = Gia_ManHashAnd( pNew, pVars[i-1], pVars[i] );
            for ( i = i+1; i < nVars; i++ )
            {
                pPerm[i-1] = pPerm[i];
                pVars[i-1] = pVars[i];
            }
            nVars--;
        }
        if ( fPrint )
        {
            for ( i = 0; i < nVars; i++ )
                printf( "%d ", pPerm[i] );
            printf( "\n" );
        }
    }
    iLit = pVars[0];
    for ( i = 1; i < nVars; i++ )
        if ( fXor )
            iLit = Gia_ManHashXor( pNew, iLit, pVars[i] );
        else if ( fOr )
            iLit = Gia_ManHashOr( pNew, iLit, pVars[i] );
        else
            iLit = Gia_ManHashAnd( pNew, iLit, pVars[i] );
    return iLit;
}
Gia_Man_t * Gia_ManMultiInputTest( int nBits )
{
    Gia_Man_t * pNew;
    int i, iRes, * pPerm;
    int * pMulti = Gia_ManCollectLiterals( nBits );
    pNew = Gia_ManStart( 1000 );
    pNew->pName = Abc_UtilStrsav( "multi" );
    for ( i = 0; i < nBits; i++ )
        Gia_ManAppendCi( pNew );
    Gia_ManHashAlloc( pNew );
    pPerm = Gia_ManGenPerm2( nBits );
    //pPerm = Gia_ManGenZero( nBits );
    iRes = Gia_ManMultiInputPerm( pNew, pMulti, nBits, pPerm, 0, 0 );
    Gia_ManAppendCo( pNew, iRes );
    ABC_FREE( pPerm );
    ABC_FREE( pMulti );
    return pNew;
}


/**Function*************************************************************

  Synopsis    [Create MUX tree.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Gia_ManCube( Gia_Man_t * pNew, int Vars, int nVars, int * pLits )
{
    int i, iLit = 1;
    for ( i = 0; i < nVars; i++ )
        iLit = Gia_ManHashAnd( pNew, iLit, Abc_LitNotCond(pLits[i], !((Vars >> i) & 1)) );
    return iLit;
}
int Gia_ManMuxTree_rec( Gia_Man_t * pNew, int * pCtrl, int nCtrl, int * pData )
{
    int iLit0, iLit1;
    if ( nCtrl == 0 )
        return pData[0];
    iLit0 = Gia_ManMuxTree_rec( pNew, pCtrl, nCtrl-1, pData );
    iLit1 = Gia_ManMuxTree_rec( pNew, pCtrl, nCtrl-1, pData + (1<<(nCtrl-1)) );
    return Gia_ManHashMux( pNew, pCtrl[nCtrl-1], iLit1, iLit0 );
}
void Gia_ManUsePerm( int * pTree, int nBits, int * pPerm )
{
    int fPrint = 0;
    int i, k, m, nVars = nBits + (1 << nBits);
    if ( fPrint )
    {
        for ( i = 0; i < nVars; i++ )
            printf( "%d ", pPerm[i] );
        printf( "\n" );
    }
    for ( i = 0; i < nBits; i++ )
    {
        for ( k = i+1; k < nBits; k++ )
            if ( pPerm[i] > pPerm[k] )
                break;
        if ( k == nBits )
            break;
        assert( pPerm[i] > pPerm[k] );
        ABC_SWAP( int, pPerm[i], pPerm[k] );
        ABC_SWAP( int, pTree[i], pTree[k] );
        for ( m = 0; m < (1 << nBits); m++ )
            if ( ((m >> i) & 1) && !((m >> k) & 1) )
            {
                ABC_SWAP( int, pTree[nBits+m], pTree[nBits+(m^(1<<i)^(1<<k))] );
                ABC_SWAP( int, pPerm[nBits+m], pPerm[nBits+(m^(1<<i)^(1<<k))] );
            }
    }
    if ( fPrint )
    {
        for ( i = 0; i < nVars; i++ )
            printf( "%d ", pPerm[i] );
        printf( "\n" );
    }
}
int Gia_ManFindCond( int * pLits, int nBits, int iLate1, int iLate2 )
{
    int i;
    assert( iLate1 != iLate2 );
    for ( i = 0; i < nBits; i++ )
        if ( (((iLate1 ^ iLate2) >> i) & 1) )
            return Abc_LitNotCond( pLits[i], (iLate1 >> i) & 1 );
    return -1;
}
int Gia_ManLatest( int * pPerm, int nVars, int iPrev1, int iPrev2, int iPrev3 )
{
    int i, Value = -1, iLate = -1;
    for ( i = 0; i < nVars; i++ )
        if ( Value < pPerm[i] && i != iPrev1 && i != iPrev2 && i != iPrev3 )
        {
            Value = pPerm[i];
            iLate = i;
        }
    return iLate;
}
int Gia_ManEarliest( int * pPerm, int nVars )
{
    int i, Value = ABC_INFINITY, iLate = -1;
    for ( i = 0; i < nVars; i++ )
        if ( Value > pPerm[i] )
        {
            Value = pPerm[i];
            iLate = i;
        }
    return iLate;
}
int Gia_ManDecompOne( Gia_Man_t * pNew, int * pTree, int nBits, int * pPerm, int iLate )
{
    int iRes, iData;
    assert( iLate >= 0 && iLate < (1<<nBits) );
    iData = pTree[nBits+iLate];
    pTree[nBits+iLate] = pTree[nBits+(iLate^1)];
    iRes = Gia_ManMuxTree_rec( pNew, pTree, nBits, pTree+nBits );
    return Gia_ManHashMux( pNew, Gia_ManCube(pNew, iLate, nBits, pTree), iData, iRes );
}
int Gia_ManDecompTwo( Gia_Man_t * pNew, int * pTree, int nBits, int * pPerm, int iLate1, int iLate2 )
{
    int iRes, iData1, iData2, iData, iCond, iCond2;
    assert( iLate1 != iLate2 );
    assert( iLate1 >= 0 && iLate1 < (1<<nBits) );
    assert( iLate2 >= 0 && iLate2 < (1<<nBits) );
    iData1 = pTree[nBits+iLate1];
    iData2 = pTree[nBits+iLate2];
    pTree[nBits+iLate1] = pTree[nBits+(iLate1^1)];
    pTree[nBits+iLate2] = pTree[nBits+(iLate2^1)];
    iRes   = Gia_ManMuxTree_rec( pNew, pTree, nBits, pTree+nBits );
    iCond  = Gia_ManHashOr( pNew, Gia_ManCube(pNew, iLate1, nBits, pTree), Gia_ManCube(pNew, iLate2, nBits, pTree) ); 
    iCond2 = Gia_ManFindCond( pTree, nBits, iLate1, iLate2 );
    iData  = Gia_ManHashMux( pNew, iCond2, iData2, iData1 );
    return Gia_ManHashMux( pNew, iCond, iData, iRes );
}
int Gia_ManDecompThree( Gia_Man_t * pNew, int * pTree, int nBits, int * pPerm, int iLate1, int iLate2, int iLate3 )
{
    int iRes, iData1, iData2, iData3, iCube1, iCube2, iCube3, iCtrl0, iCtrl1, iMux10, iMux11;
    assert( iLate1 != iLate2 );
    assert( iLate1 != iLate3 );
    assert( iLate2 != iLate3 );
    assert( iLate1 >= 0 && iLate1 < (1<<nBits) );
    assert( iLate2 >= 0 && iLate2 < (1<<nBits) );
    assert( iLate3 >= 0 && iLate3 < (1<<nBits) );
    iData1 = pTree[nBits+iLate1];
    iData2 = pTree[nBits+iLate2];
    iData3 = pTree[nBits+iLate3];
    pTree[nBits+iLate1] = pTree[nBits+(iLate1^1)];
    pTree[nBits+iLate2] = pTree[nBits+(iLate2^1)];
    pTree[nBits+iLate3] = pTree[nBits+(iLate3^1)];
    iRes   = Gia_ManMuxTree_rec( pNew, pTree, nBits, pTree+nBits );
    iCube1 = Gia_ManCube( pNew, iLate1, nBits, pTree );
    iCube2 = Gia_ManCube( pNew, iLate2, nBits, pTree );
    iCube3 = Gia_ManCube( pNew, iLate3, nBits, pTree );
    iCtrl0 = Gia_ManHashOr( pNew, iCube1, iCube3 );
    iCtrl1 = Gia_ManHashOr( pNew, iCube2, iCube3 );
    iMux10 = Gia_ManHashMux( pNew, iCtrl0, iData1, iRes );
    iMux11 = Gia_ManHashMux( pNew, iCtrl0, iData3, iData2 );
    return Gia_ManHashMux( pNew, iCtrl1, iMux11, iMux10 );
}
int Gia_ManDecomp( Gia_Man_t * pNew, int * pTree, int nBits, int * pPerm )
{
    if ( nBits == 2 )
        return Gia_ManMuxTree_rec( pNew, pTree, nBits, pTree+nBits );
    else
    {
        int iBase  = Gia_ManEarliest( pPerm+nBits, 1<<nBits ), BaseValue = pPerm[nBits+iBase];
        int iLate1 = Gia_ManLatest( pPerm+nBits, 1<<nBits, -1, -1, -1 );
        int iLate2 = Gia_ManLatest( pPerm+nBits, 1<<nBits, iLate1, -1, -1 );
        int iLate3 = Gia_ManLatest( pPerm+nBits, 1<<nBits, iLate1, iLate2, -1 );
        int iLate4 = Gia_ManLatest( pPerm+nBits, 1<<nBits, iLate1, iLate2, iLate3 );
        if ( 0 )
        {
            int i;
            for ( i = 0; i < (1<<nBits); i++ )
                printf( "%d ", pPerm[nBits+i] );
            printf( "\n" );
        }
        if ( pPerm[nBits+iLate1] > BaseValue && pPerm[nBits+iLate2] > BaseValue && pPerm[nBits+iLate3] > BaseValue && pPerm[nBits+iLate4] == BaseValue )
            return Gia_ManDecompThree( pNew, pTree, nBits, pPerm, iLate1, iLate2, iLate3 );
        if ( pPerm[nBits+iLate1] > BaseValue && pPerm[nBits+iLate2] > BaseValue && pPerm[nBits+iLate3] == BaseValue )
            return Gia_ManDecompTwo( pNew, pTree, nBits, pPerm, iLate1, iLate2 );
        if ( pPerm[nBits+iLate1] > BaseValue && pPerm[nBits+iLate2] == BaseValue )
            return Gia_ManDecompOne( pNew, pTree, nBits, pPerm, iLate1 );
        return Gia_ManMuxTree_rec( pNew, pTree, nBits, pTree+nBits );
    }
}
Gia_Man_t * Gia_ManMuxTreeTest( int nBits )
{
    Gia_Man_t * pNew;
    int i, iLit, nVars = nBits + (1 << nBits);
    int * pPerm, * pTree = Gia_ManCollectLiterals( nVars );
    pNew = Gia_ManStart( 1000 );
    pNew->pName = Abc_UtilStrsav( "mux_tree" );
    for ( i = 0; i < nVars; i++ )
        Gia_ManAppendCi( pNew );
    Gia_ManHashAlloc( pNew );
    pPerm = Gia_ManGenPerm( nVars );
    //pPerm = Gia_ManGenZero( nVars );
    pPerm[nBits+1] = 100;
    pPerm[nBits+5] = 100;
    pPerm[nBits+4] = 100;
    Gia_ManUsePerm( pTree, nBits, pPerm );
    iLit = Gia_ManDecomp( pNew, pTree, nBits, pPerm );
    Gia_ManAppendCo( pNew, iLit );
    ABC_FREE( pPerm );
    ABC_FREE( pTree );
    return pNew;
}

////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_IMPL_END

