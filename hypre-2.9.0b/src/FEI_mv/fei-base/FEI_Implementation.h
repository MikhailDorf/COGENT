
#ifndef _FEI_Implementation_h_
#define _FEI_Implementation_h_

/*--------------------------------------------------------------------*/
/*    Copyright 2005 Sandia Corporation.                              */
/*    Under the terms of Contract DE-AC04-94AL85000, there is a       */
/*    non-exclusive license for use of this work by or on behalf      */
/*    of the U.S. Government.  Export of this program may require     */
/*    a license from the United States Government.                    */
/*--------------------------------------------------------------------*/

#include <base/snl_fei_fwd.h>
#include <base/fei_mpi.h>
#include <fei_SharedPtr.h>
#include <fei_defs.h>


#include <base/FEI_SNL.h>

#include <base/feiArray.h>

/**
This is the (C++) user's point of interaction with the FEI implementation. The
user will declare an instance of this class in their code, and call the public
FEI functions on that instance. The functions implemented by this class are
those in the abstract FEI declaration, plus possibly others. i.e., the functions
provided by this class are a superset of those in the FEI specification.
<p>
This class takes, as a constructor argument, a 'LibraryWrapper' object which is a
shell containing only an instance of a LinearSystemCore implementation or a
FiniteElementData implementation. These are the abstract interfaces through which
solver libraries may be coupled to this FEI implementation.<p>
As of August 2001, the following solver implementations of these interfaces
exist:<p>
<ul>
<li>LinearSystemCore:
   <ul>
   <li>Aztec
   <li>HYPRE
   <li>ISIS++
   <li>PETSc
   <li>Prometheus
   <li>SPOOLES
   </ul>
<li>FiniteElementData:
   <ul>
   <li>FETI-DP
   </ul>
</ul>

 */

class FEI_Implementation : public FEI_SNL {

 public:
  /**  constructor.
      @param libWrapper Simple container that holds either a LinearSystemCore
      or FiniteElementData instance.
      @param comm MPI_Comm communicator
      @param masterRank The "master" mpi rank. Defaults to 0 if not supplied.
      This is not an important parameter, simply determining which processor
      will produce screen output if the parameter "outputLevel" is set to a
      value greater than 0 via a call to the parameters function.
  */
   FEI_Implementation(fei::SharedPtr<LibraryWrapper> libWrapper,
		      MPI_Comm comm,
                      int masterRank=0);

   /** Destructor. */
   virtual ~FEI_Implementation();

//public FEI functions:


    /** Set parameters associated with solver choice, etc. This function may be
       called at any time after the FEI object is instantiated. This function
       may be called repeatedly with different parameters, which will accumulate
       those parameters into an internal 'master'-list of parameters.
       @param numParams Number of parameters being supplied.
       @param paramStrings List of 'numParams' strings. Each string usually
            contains a key-value pair, separated by a space.
    */
   int parameters(int numParams, char **paramStrings);

//Structural initialization functions.............................

   /**Specify matrixIDs and rhsIDs to be used in cases where multiple matrices
      and/or rhs vectors are being assembled. This function does not need
      to be called if only one matrix and rhs are being assembled. Note: the
      values of the matrix and rhs identifiers must be non-negative. Important
      Note: If this function is called, it must be called BEFORE setSolveType
      is called. setSolveType must then be called with the parameter
      FEI_AGGREGATE_SUM (eigen-solves and product-solves aren't supported
      yet).
      @param numMatrices length of matrixIDs parameter
      @param matrixIDs list of user-defined identifiers for separate matrices
                       to be assembled
      @param numRHSs length of rhsIDs parameter
      @param rhsIDs list of user-defined identifiers for separate rhs vectors
                    to be assembled
   */
   int setIDLists(int numMatrices,
                  const int* matrixIDs,
                  int numRHSs,
                  const int* rhsIDs);

    /** Set the type of solve to be performed.
        This distinguishes between a 'standard' single solve of Ax=b,
        an eigen-solve (not yet supported), an 'aggregate-sum' solve
        'aggregate-product' solve (not supported).
        @param solveType currently supported values for this are:
                          FEI_SINGLE_SOLVE, FEI_AGGREGATE_SUM
    */
   int setSolveType(int solveType);

    /** Identify all the fields present in the analysis. A field may be a
        scalar such as temperature or pressure, or a 3-vector for velocity, etc.
        Non-solution fields may be denoted by a negative fieldID. This allows
        for situations where the application wants to pass data that the FEI
        doesn't need, through to the underlying linear algebra library. This
        may be done via the function 'putSubstructureFieldData'. (It may also
        be done via any of the various put*Solution functions.) An example of
        this could be supplying geometric coordinates to a solver that needs
        them.
        @param numFields Global number of fields in the entire problem, on all
               processors. (This is the length of the fieldSizes and fieldIDs
               lists.)
        @param fieldSizes Number of scalars contained in each field.
        @param fieldIDs User-supplied identifiers for each field.
    */
   int initFields(int numFields, 
                  const int *fieldSizes, 
                  const int *fieldIDs);

    /** Initialize the description of an element-block. This function informs
        the fei implementation of the defining characteristics for a block of
        elements. An element-block must be homogeneous -- all elements in the
        block must have the same number of nodes, same number of solution fields
        per node, etc.
        @param elemBlockID The user-defined identifier for this element-block.
        @param numElements The number of elements in this block.
        @param numNodesPerElement Length of the numFieldsPerNode list.
        @param numFieldsPerNode Lengths of the rows of the nodalFieldIDs table.
        @param nodalFieldIDs Table where row 'i' is the list of field ids for
               the ith node on every element in this element-block.
        @param numElemDofFieldsPerElement Length of the elemDOFFieldIDs list.
        @param elemDOFFieldIDs list of field identifiers for the element-
               centered degrees-of-freedom in the elements in this block.
        @param interleaveStrategy Indicates the ordering of solution-components
               in the element-wise (e.g., stiffness) contribution arrays. Valid
               values are FEI_NODE_MAJOR (all field-components for first node
               are followed by all field-components for second node, ...) or
               FEI_FIELD_MAJOR (first-field for all nodes, then second-field for
               all nodes, ...)
    */
   int initElemBlock(GlobalID elemBlockID,
                     int numElements,
                     int numNodesPerElement,
                     const int* numFieldsPerNode,
                     const int* const* nodalFieldIDs,
                     int numElemDofFieldsPerElement,
                     const int* elemDOFFieldIDs,
                     int interleaveStrategy);

     /** Initialize an element's connectivity. Provide a nodal connectivity list
        for inclusion in the sparse matrix structure being constructed.
        @param elemBlockID Which element-block this element belongs to.
        @param elemID A user-provided identifier for this element.
        @param elemConn List of nodeIDs connected to this element. Length of
               this list must be 'numNodesPerElement' provided to the function
               'initElemBlock' for this elemBlockID.
    */
  int initElem(GlobalID elemBlockID,
                GlobalID elemID,
                const GlobalID* elemConn);

       of other variables, plus a right-hand-side value (note that the rhsValue
       will often be zero). Since a field may contain more than one scalar
       component, the particular scalar equation that's being slaved must be
       specified by not only a nodeID and fieldID, but also an offset into the
       slave field.

       The general form of the dependency being specified is:
       seqn = sum ( weight_i * meqn_i ) + rhsValue
       where 'seqn' means slave-equation and 'meqn' means master equation.

       Example: to specify that a slave-equation is the average of two master-
       equations: seqn = 0.5*meqn_1 + 0.5*meqn_2 + 0.0
       (Where 0.0 is the rhsValue in this case.)

       The list of weights supplied will be assumed to be of length 
       sum(masterFieldSizes). i.e., the slave equation may be dependent on more
       than one component of the specified master field. In cases where a
       master field contains more than one scalar component, but only one of 
       those components is relevant to the dependency being specified, then 
       positions in the weights list corresponding to the non-relevant 
       field-components should contain zeros.

       This mechanism can also be used as an alternative way to specify
       essential boundary conditions, where the rhsValue will be the boundary
       condition value, with no master nodes or weights.

       Note: This is a new and experimental capability, and is not compatible
       with all other FEI capabilities. In particular, the following precaution
       should be taken:
       Don't identify both slave variables and constraint-relations for the
       same degree of freedom. They are mutually exclusive.

       @param slaveNodeID Node identifier of the node containing the slave eqn.
       @param slaveFieldID Field identifier corresponding to the slave eqn.
       @param offsetIntoSlaveField Denotes location, within the field, of the
       slave eqn.
       @param numMasterNodes Number of nodes containing variables on which the
       slave depends.
       @param masterNodeIDs Node identifiers of the master nodes.
       @param masterFieldIDs List, length numMasterNodes, of the field at each
       master-node which contains the scalar variable(s) on which the slave
       depends.
       @param weights List, length sum-of-master-field-sizes, containing the
       weighting coefficients described above.
       @param rhsValue 
   */
   int initSlaveVariable(GlobalID slaveNodeID, 
			 int slaveFieldID,
			 int offsetIntoSlaveField,
			 int numMasterNodes,
			 const GlobalID* masterNodeIDs,
			 const int* masterFieldIDs,
			 const double* weights,
			 double rhsValue);

   /** Request that any existing Lagrange-Multiplier constraints be deleted.
       (Intended to be called in preparation for loading new/different
       constraints.)
   */
   int deleteMultCRs();

   /** identify sets of shared nodes */
   int initSharedNodes(int numSharedNodes,
                       const GlobalID *sharedNodeIDs,  
                       const int* numProcsPerNode, 
                       const int *const *sharingProcIDs);

    /** Constraint relation initialization, Lagrange Multiplier formulation.
        @param numCRNodes Length of the CRNodeIDs and CRFieldIDs lists.
        @param CRNodes Nodes involved in this constraint relation.
        @param CRFields List of the the field being constrained at each node.
        @param CRID Output. An identifier by which this constraint relation may
               be referred to later, when loading its weight data and recovering
               its Lagrange Multiplier after the solve.
    */
   int initCRMult(int numCRNodes,
                  const GlobalID* CRNodes,
                  const int *CRFields,
                  int& CRID); 

    /** Constraint relation initialization, Penalty function formulation .
        @param numCRNodes Length of the CRNodeIDs and CRFieldIDs lists.
        @param CRNodes Nodes involved in this constraint relation.
        @param CRFields List of the the field being constrained at each node.
        @param CRID Output. An identifier by which this constraint relation may
               be referred to later, when loading its weight data and penalty
               value.
    */
   int initCRPen(int numCRNodes,
                 const GlobalID* CRNodes, 
                 const int *CRFields,
                 int& CRID); 

   /** Initialization of a pattern by which assembled coefficients will be
       accessed. This is analogous to the 'initElemBlock' function, whereby the
       mask or description of elements in an element-block is provided.
       Locations in the matrix or vector are specified using pairs of either
       nodeIDs and fieldIDs or elementIDs and fieldIDs. For elementIDs, the
       coefficient-access refers to element-centered coefs, or elem-dof.
       An access pattern can be used to specify access to a rectangular region
       of coefficients, with the following limitation. The mask of fields-per-
       column-entity must be the same for each row in the pattern. The number of coefficients in each row of the accessed region will
       be the same.
       @param patternID User-supplied identifier, to be referred to later when
              calling 'initCoefAccess', 'getCoefMatrix', etc.
       @param numRowIDs Number of node IDs or element IDs for which
       corresponding rows will be accessed. (Length of the numFieldsPerRow
       list, and number of rows in the rowFieldIDs table.)
       @param numFieldsPerRow
       @param rowFieldIDs For each row-'entity' (nodeID or elemID), a list of 
       fieldIDs for which rows of coeficients will be accessed.
       @param numColIDsPerRow Number of IDs for which corresponding columns will
              be accessed in each row. (Length of the numFieldsPerCol list, and
	      number of rows in the colFieldIDs table.)
	      @param numFieldsPerCol
       @param colFieldIDs For each column-'entity', a list of fieldIDs for which
              columns of the matrix will be accessed.
       @param interleaveStrategy Same valid values here as for the parameter of
              the same name in the function 'initElemBlock'.
   */
   int initCoefAccessPattern(int patternID,
                             int numRowIDs,
                             const int* numFieldsPerRow,
                             const int* const* rowFieldIDs,
                             int numColIDsPerRow,
                             const int* numFieldsPerCol,
                             const int* const* colFieldIDs,
                             int interleaveStrategy);

   /** Initialization of coefficient access for specific rows/columns, using a
       user-defined pattern. This will
       cause positions to be inserted into the underlying sparse matrix 
       structure if they don't already exist.
       @param patternID Access pattern which must already have been defined by
              a call to the function 'initCoefAccessPattern'.
       @param rowIDTypes List whose entries can take the value FEI_NODE or
              FEI_ELEMENT. Entries indicate whether the corresponding entry in
              'rowIDs' is a nodeID or an elementID.
       @param rowIDs List of node or element identifiers for which
       corresponding rows are to be accessed. Length was defined when
       'initCoefAccessPattern' was  called.
       @param colIDTypes List whose entries can take the value FEI_NODE or
              FEI_ELEMENT. Entries indicate whether the corresponding entry in
              'colIDs' is a nodeID or an elementID. The length of this list is
	      assumed to be 'numRowIDs' X 'numColIDsPerRow' (these parameters
	      were supplied when the pattern was initialized via
	      'initCoefAccessPattern').
       @param colIDs List of identifiers for which corresponding columns are to
       be accessed. Length is the same as 'colIDTypes'.
   */ 
   int initCoefAccess(int patternID,
		      const int* rowIDTypes,
                      const GlobalID* rowIDs,
		      const int* colIDTypes,
                      const GlobalID* colIDs);

   /** Initialize a substructure. By convention, substructureID 0 will refer
       to all locally active nodes. substructureID 0 need not be initialized.
       Note: nodes that appear in substructures must also appear elsewhere in
       the initialization phase, either in element connectivities or as nodes
       in access patterns.
       @param substructureID User-supplied identifier with which to refer to
               this substructure later. NOTE: This should be an integer that is
             greater than 0. 0 is reserved for the substructure that refers to
            all local nodes.
       @param numIDs Length of the IDTypes and IDs lists.
       @param IDTypes List whose entries can take the value FEI_NODE or
       FEI_ELEMENT. Entries indicate whether the corresponding entry in IDs is
       referring to a nodeID or an elemID.
       @param IDs User-supplied list of identifiers to associate with this
                substructure.
   */
   int initSubstructure(int substructureID,
                        int numIDs,
			const int* IDTypes,
                        const GlobalID* IDs);

   /** indicate that overall initialization sequence is complete */
   int initComplete();

// FEI data loading sequence..........................................

   /** direct data to a specific internal data structure
   i.e., set the current matrix 'context'. */
   int setCurrentMatrix(int matID);

   /**direct data to a specific internal data structure
   i.e., set the current RHS 'context'. */
   int setCurrentRHS(int rhsID);

   /** set a value (usually zeros) throughout the linear system */
   int resetSystem(double s=0.0);

   /** set a value (usually zeros) throughout the matrix or rhs-vector
       separately */
   int resetMatrix(double s=0.0);

    /** Set a value (usually zero) througout the rhs vector.
        @param s The value to be written into the rhs vector.
    */
   int resetRHSVector(double s=0.0);

    /** Set a value (usually, if not always, 0.0) throughout the initial guess
       (solution) vector.
       @param s Input. Scalar value to use in filling the solution vector.
       @return error-code 0 if successful
   */
  int resetInitialGuess(double s=0.0);

    /** Load nodal boundary condition data. This allows the application to
       specify a
       boundary condition (dirichlet, neumann or mixed) on a list of nodes.
       The form of these boundary conditions is as follows for a given
       node/field upon which the condition is to be imposed (description
       courtesy of Kim Mish). If the primary
       field solution unknown is denoted by u, and the dual of the solution
       (e.g., force as opposed to displacement) is denoted by q, then a generic
       boundary condition can be specified by alpha*u + beta*q = gamma. A
       dirichlet boundary condition is given when alpha is non-zero but beta is
       zero. A neumann boundary condition is given when alpha is zero but beta
       is non-zero. A mixed boundary condition is given when alpha and beta are
       both non-zero.
       The boundary condition specified via this function applies to the same
       solution field on all nodes in the nodeIDs list.
       @param numNodes Length of the nodeIDs list.
       @param nodeIDs List of nodes upon which a boundary condition is to be
               imposed.
       @param fieldID The solution field that will receive the boundary
                   condition.
       @param alpha Table, with 'numNodes' number-of-rows, and the length of
               each row being the number of scalars that make up the solution
               field 'fieldID'.
       @param beta Table, same dimensions as alpha.
       @param gamma Table, same dimensions as alpha.
    */
    int loadNodeBCs(int numNodes,
                    const GlobalID *nodeIDs,  
                    int fieldID,
                    const double *const *alpha,  
                    const double *const *beta,  
                    const double *const *gamma);

    /** Load boundary condition data for element-dof.
        Similar to the function 'loadNodeBCs', as far as the definition of the
        coefficients alpha, beta and gamma.
        @param numElems Length of the elemIDs list.
        @param elemIDs List of elements for which a boundary condition is to be
               specified.
        @param fieldID The solution field for which to apply the boundary
              condition.
        @param alpha Table, as in 'loadNodeBCs', but with 'numElems' number-of-
              rows.
        @param beta Table, same dimensions as alpha.
        @param gamma Table, same dimensions as alpha.
    */
    int loadElemBCs(int numElems,
                    const GlobalID* elemIDs,  
                    int fieldID,
                    const double *const *alpha,  
                    const double *const *beta,  
                    const double *const *gamma);

    /** Element-stiffness/load data loading. This function accumulates element
       stiffness and load data into the underlying matrix and rhs vector.
       @param elemBlockID Which element-block this element belongs to.
       @param elemID User-supplied identifier for this element.
       @param elemConn Connectivity list of nodes that are connected to this
             element.
       @param elemStiffness Table of element-stiffness data. Dimensions of this
             table defined by the sum of the sizes of the fields associated with
             this element. (This information supplied earlier via
             'initElemBlock'.)
       @param elemLoad Element-load vector.
       @param elemFormat Designates the way in which the 'elemStiffness' 
              stiffness-matrix data is laid out. Valid values for this parameter
              can be found in the file fei_defs.h.
   */
   int sumInElem(GlobalID elemBlockID,
                 GlobalID elemID,
                 const GlobalID* elemConn,
                 const double* const* elemStiffness,
                 const double* elemLoad,
                 int elemFormat);

    /** Element-stiffness data loading. This function is the same as 'sumInElem'
       but only accepts stiffness data, not the load data for the rhs.
       @param elemBlockID Which element-block this element belongs to.
       @param elemID User-supplied identifier for this element.
       @param elemConn Connectivity list of nodes that are connected to this
             element.
       @param elemStiffness Table of element-stiffness data. Dimensions of this
             table defined by the sum of the sizes of the fields associated with
             this element. (This information supplied earlier via
             'initElemBlock'.)
       @param elemFormat Designates the way in which the 'elemStiffness'
              stiffness-matrix data is laid out. Valid values for this parameter
              can be found in the file fei_defs.h.
   */
   int sumInElemMatrix(GlobalID elemBlockID,
                       GlobalID elemID,
                       const GlobalID* elemConn,
                       const double* const* elemStiffness,
                       int elemFormat);

    /** Element-load data loading. This function is the same as 'sumInElem',
       but only accepts the load for the rhs, not the stiffness matrix.
       @param elemBlockID Which element-block this element belongs to.
       @param elemID User-supplied identifier for this element.
       @param elemConn Connectivity list of nodes that are connected to this
             element.
       @param elemLoad Element-load vector.
   */
   int sumInElemRHS(GlobalID elemBlockID,
                    GlobalID elemID,
                    const GlobalID* elemConn,
                    const double* elemLoad);

   /** element-wise transfer operator */
   int loadElemTransfer(GlobalID elemBlockID,
                        GlobalID elemID,
                        const GlobalID* coarseNodeList,
                        int fineNodesPerCoarseElem,
                        const GlobalID* fineNodeList,
                        const double* const* elemProlong,
                        const double* const* elemRestrict);

    /** Load weight/value data for a Lagrange Multiplier constraint relation.
       @param CRID Identifier returned from an earlier call to 'initCRMult'.
       @param numCRNodes Length of CRNodeIDs and CRFieldIDs lists.
       @param CRNodes List of nodes in this constraint relation.
       @param CRFields List of fields, one per node, to be constrained.
       @param CRWeights Weighting coefficients. This length of this list is the
       sum of the sizes associated with the fields identified in CRFieldIDs.
       @param CRValue The constraint's rhs value. Often (always?) zero.
    */
   int loadCRMult(int CRID,
                  int numCRNodes,
                  const GlobalID* CRNodes,
                  const int* CRFields,
                  const double* CRWeights,
                  double CRValue);

    /** Load weight/value data for a Penalty constraint relation.
       @param CRID Identifier returned from an earlier call to 'initCRPen'.
       @param numCRNodes Length of CRNodeIDs and CRFieldIDs lists.
       @param CRNodes List of nodes in this constraint relation.
       @param CRFields List of fields, one per node, to be constrained.
       @param CRWeights Weighting coefficients. This length of this list is the
       sum of the sizes associated with the fields identified in CRFieldIDs.
       @param CRValue The constraint's rhs value. Often (always?) zero.
       @param penValue The penalty value.
    */
   int loadCRPen(int CRID,
                 int numCRNodes,
                 const GlobalID* CRNodes,
                 const int* CRFields,
                 const double* CRWeights,
                 double CRValue,
                 double penValue);

   /** Accumulate coefficients into the matrix. 
      @param patternID User-defined identifier used in an earlier call to the
            function 'initCoefAccessPattern'.
      @param rowIDTypes List whose entries can take the value FEI_NODE or
             FEI_ELEMENT. Entries indicate whether the corresponding entry in
             'rowIDs' is a nodeID or an elementID.
      @param rowIDs List of nodes or elements for which the corresponding
      rows in the matrix will be accessed.
       @param colIDTypes List whose entries can take the value FEI_NODE or
              FEI_ELEMENT. Entries indicate whether the corresponding entry in
              'rowIDs' is a nodeID or an elementID.
      @param colIDs List of nodes or elements for which the corresponding
      columns in the matrix will be accessed. This list is assumed to be of
      length 'numRowIDs' X 'numColIDsPerRow' (parameters passed when the
      access-pattern was initialized).
      @param matrixEntries Table of coefficient values. Number of rows given by
           the sum of the sizes of the fields accessed for rowIDs (data
           provided in the earlier call to 'initCoefAccessPattern'). Number of
           columns given by the sum of the sizes of the fields accessed for
           colIDs.
   */
   int sumIntoMatrix(int patternID,
		     const int* rowIDTypes,
                     const GlobalID* rowIDs,
		     const int* colIDTypes,
                     const GlobalID* colIDs,
                     const double* const* matrixEntries);

   /** Sum a copy of coefficient data into the rhs vector. */
   int sumIntoRHS(int patternID,
		  const int* IDTypes,
                  const GlobalID* IDs,
                  const double* rhsEntries);

   /** Put a copy of coefficients into the matrix.
      @param patternID User-defined identifier used in an earlier call to the
            function 'initCoefAccessPattern'.
      @param rowIDTypes Same meaning as 'sumIntoMatrix' argument of same name
      @param rowIDs List of nodes for which the corresponding rows in the
            matrix will be accessed.
      @param colIDTypes Same meaning as 'sumIntoMatrix' argument of same name
      @param colIDs List of nodes for which the corresponding columns in the
            matrix will be accessed. This list is assumed to be of
      length 'numRowIDs' X 'numColIDsPerRow' (parameters passed when the
      access-pattern was initialized).
      @param matrixEntries Table of coefficient values. Number of rows given by
           the sum of the sizes of the fields accessed for rowIDs (data
           provided in the earlier call to 'initCoefAccessPattern'). Number of
           columns given by the sum of the sizes of the fields accessed for
           colIDs.
   */
   int putIntoMatrix(int patternID,
		     const int* rowIDTypes,
                     const GlobalID* rowIDs,
		     const int* colIDTypes,
                     const GlobalID* colIDs,
                     const double* const* matrixEntries);

   /** Put a copy of coefficient data into the rhs vector. */
   int putIntoRHS(int patternID,
		  const int* IDTypes,
                  const GlobalID* IDs,
                  const double* rhsEntries);

   /** Put a copy of coefficient data into the rhs vector. */
   int putIntoRHS(int IDType,
		  int fieldID,
		  int numIDs,
		  const GlobalID* IDs,
		  const double* rhsEntries);

   /** Sum a copy of coefficient data into the rhs vector. */
   int sumIntoRHS(int IDType,
		  int fieldID,
		  int numIDs,
		  const GlobalID* IDs,
		  const double* rhsEntries);

   /** Get a copy of coefficients from the matrix. A special semantic applies
       to the getFromMatrix (and getFromRHS) function if any of the IDs in
       the argument 'rowIDs' correspond to shared nodes that are not locally
       owned. Equations for shared nodes reside on only 1 'owning' processor,
       with the owning processor being chosen by the FEI implementation. The
       calling code has no way to know which processor 'owns' a shared node.
       Thus if this call is made only on this processor, then the coefficients
       for equations (matrix rows) that correspond to remotely-owned shared 
       nodes will not be returned. Those positions in the 'matrixEntries'
       argument will not be referenced. If this function call is made on all
       sharing processors, then an attempt will be made to perform the
       communication to get those remotely owned coefficients from the owning
       processor. (This will probably be done using MPI_Iprobe calls to see if
       the owning/sharing processor is in this function and able to be
       communicated with. So not all processors need to make this call, only 
       all processors that share the node(s) for which matrix row entries are
       being requested.)
      @param patternID User-defined identifier used in an earlier call to the
            function 'initCoefAccessPattern'.
       @param rowIDTypes List whose entries can take the value FEI_NODE or
              FEI_ELEMENT. Entries indicate whether the corresponding entry in
              'rowIDs' is a nodeID or an elementID.
      @param rowIDs List of nodes or elements for which the corresponding rows
      in the matrix will be accessed.
       @param colIDTypes List whose entries can take the value FEI_NODE or
              FEI_ELEMENT. Entries indicate whether the corresponding entry in
              'rowIDs' is a nodeID or an elementID.
      @param colIDs List of nodes for which the corresponding columns in the
            matrix will be accessed. This list is assumed to be of
      length 'numRowIDs' X 'numColIDsPerRow' (parameters passed when the
      access-pattern was initialized).
      @param matrixEntries Table of coefficient values. Number of rows given by
           the sum of the sizes of the fields accessed for rowIDs (data
           provided in the earlier call to 'initCoefAccessPattern'). Number of
           columns given by the sum of the sizes of the fields accessed for
           colIDs.
   */
   int getFromMatrix(int patternID,
		     const int* rowIDTypes,
                     const GlobalID* rowIDs,
		     const int* colIDTypes,
                     const GlobalID* colIDs,
                     double** matrixEntries);

   /** Get a copy of coefficient data from the rhs vector.
      @param patternID User-defined identifier used in an earlier call to the
            function 'initCoefAccessPattern'.
      @param IDTypes
      @param IDs
      @param rhsEntries List of coefficient values, length is the same as the
            number of rows in the 'matrixEntries' argument of the functions that
           access elem-dof matrix coefficients.
   */
   int getFromRHS(int patternID,
		  const int* IDTypes,
                  const GlobalID* IDs,
                  double* rhsEntries);

// Equation solution services.....................................

     system of matrices. */
   int setMatScalars(int numScalars,
                     const int* IDs, 
                     const double* scalars);

   /** set scalar coefficients for aggregating RHS vectors. */
   int setRHSScalars(int numScalars,
                     const int* IDs,
                     const double* scalars);

   /**indicate that the matrix/vectors can be finalized now. e.g., boundary-
   conditions enforced, etc., etc. */
   int loadComplete();

   /** get residual norms */
   int residualNorm(int whichNorm,
                    int numFields,
                    int* fieldIDs,
                    double* norms);

   /** launch underlying solver */
   int solve(int& status);

   /** Query number of iterations taken for last solve.
      @param itersTaken Iterations performed during any previous solve.
   */
   int iterations(int& itersTaken) const;

   /** Return a version string. This string is owned by the FEI implementation,
       the calling application should not delete/free it.
      This string will contain the FEI implementation's version number, and
      if possible, a build time/date.
      @param versionString Output reference to a char*. The C interface will
         have a char** here. This function is simply setting versionString to
         point to the internal version string.
   */
   int version(const char*& versionString);

   /** query for some accumulated timing information.*/
   int cumulative_cpu_times(double& initTime,
			    double& loadTime,
			    double& solveTime,
			    double& solnReturnTime);

   /** query the amount of memory currently allocated within this implementation*/
   int allocatedSize(int& bytes);

// Solution return services.......................................
 
   /** return all nodal solution params on a block-by-block basis */
    int getBlockNodeSolution(GlobalID elemBlockID,  
                             int numNodes, 
                             const GlobalID *nodeIDs, 
                             int *offsets,
                             double *results);

    /** return all nodal solution params for an arbitrary list of nodes */
    int getNodalSolution(int numNodes,
			 const GlobalID* nodeIDs,
			 int* offsets,
			 double* results);

    /** return nodal solution for one field on a block-by-block basis */
    int getBlockFieldNodeSolution(GlobalID elemBlockID,
                                  int fieldID,
                                  int numNodes, 
                                  const GlobalID *nodeIDs, 
                                  double *results);
         
    /** return element solution params on a block-by-block basis */
    int getBlockElemSolution(GlobalID elemBlockID,  
                             int numElems, 
                             const GlobalID *elemIDs,
                             int& numElemDOFPerElement,
                             double *results);

    /** Query number of lagrange-multiplier constraints on local processor */
   int getNumCRMultipliers(int& numMultCRs);

   /** Obtain list of lagrange-multiplier IDs */
   int getCRMultIDList(int numMultCRs, int* multIDs);

   /** get Lagrange Multipliers */
   int getCRMultipliers(int numCRs,
                        const int* CRIDs,
                        double *multipliers);

   /** Query size of a substructure */
   int getSubstructureSize( int substructureID,
			    int& numIDs );

   /** Obtain list of node IDs for a substructure */
   int getSubstructureIDList(int substructureID,
			     int numNodes,
			     int* IDTypes,
			     GlobalID* IDs );

   /** Obtain solution for nodes on a substructure */
   int getSubstructureFieldSolution(int substructureID,
				    int fieldID,
				    int numIDs,
				    const int* IDTypes,
				    const GlobalID *IDs,
				    double *results);

   /** Put initial-guess for nodes on a substructure */
   int putSubstructureFieldSolution(int substructureID,
				    int fieldID,
				    int numIDs,
				    const int* IDTypes,
				    const GlobalID *IDs,
				    const double *estimates);

   /** Input data associated with a specified field for nodes on a substructure*/
   int putSubstructureFieldData(int substructureID,
				int fieldID,
				int numNodes,
				const int* IDTypes,
				const GlobalID *nodeIDs,
				const double *data);   

 
// associated "puts" paralleling the solution return services.
// 
// the int sizing parameters are passed for error-checking purposes, so
// that the interface implementation can tell if the passed estimate
// vectors make sense -before- an attempt is made to utilize them as
// initial guesses by unpacking them into the solver's native solution
// vector format (these parameters include lenNodeIDList, lenElemIDList,
// numElemDOF, and numMultCRs -- all other passed params are either 
// vectors or block/constraint-set IDs)

   /** put nodal-based solution guess on a block-by-block basis */
    int putBlockNodeSolution(GlobalID elemBlockID, 
                             int numNodes, 
                             const GlobalID *nodeIDs, 
                             const int *offsets,
                             const double *estimates);

    /** put nodal-based guess for one field on a block-by-block basis */
    int putBlockFieldNodeSolution(GlobalID elemBlockID, 
                                  int fieldID, 
                                  int numNodes, 
                                  const GlobalID *nodeIDs, 
                                  const double *estimates);
         
    /** put element-based solution guess on a block-by-block basis */
    int putBlockElemSolution(GlobalID elemBlockID,  
                             int numElems, 
                             const GlobalID *elemIDs, 
                             int dofPerElem,
                             const double *estimates);

    /** put Lagrange solution to FE analysis on a constraint-set basis */
    int putCRMultipliers(int numMultCRs, 
                         const int* CRIDs,
                         const double* multEstimates);

// utility functions that aid in integrating the FEI calls..............

// support methods for the "gets" and "puts" of the soln services.


    /** return info associated with blocked nodal solution */
    int getBlockNodeIDList(GlobalID elemBlockID,
                           int numNodes,
                           GlobalID *nodeIDs);

    /** return info associated with blocked element solution */
   int getBlockElemIDList(GlobalID elemBlockID, 
                          int numElems, 
                          GlobalID* elemIDs);
 
// miscellaneous self-explanatory "read-only" query functions............ 
 
   /** Query number of degrees-of-freedom for specified node */
    int getNumSolnParams(GlobalID nodeID, int& numSolnParams) const;

    /** Query number of element-blocks on local processor */
    int getNumElemBlocks(int& numElemBlocks) const;

    /**  return the number of active nodes in a given element block */
    int getNumBlockActNodes(GlobalID blockID, int& numNodes) const;

    /**  return the number of active equations in a given element block */
    int getNumBlockActEqns(GlobalID blockID, int& numEqns) const;

    /**  return the number of nodes associated with elements of a
	 given block ID */
    int getNumNodesPerElement(GlobalID blockID, int& nodesPerElem) const;
    
    /**  return the number of equations (including element eqns)
	 associated with elements of a given block ID */
    int getNumEqnsPerElement(GlobalID blockID, int& numEqns) const;

    /**  return the number of elements associated with this blockID */
    int getNumBlockElements(GlobalID blockID, int& numElems) const;

    /**  return the number of elements eqns for elems w/ this blockID */
    int getNumBlockElemDOF(GlobalID blockID, int& DOFPerElem) const;


    /** return the parameters that have been set so far. The caller should
     NOT delete the paramStrings pointer.
    */
    int getParameters(int& numParams, char**& paramStrings);

    //And now a couple of non-FEI query functions that Sandia applications
    //need to augment the matrix-access functions. I (Alan Williams) will
    //argue to have these included in the FEI 2.1 specification update.

    /** Query the size of a field. This info is supplied to the FEI (initFields)
	by the application, but may not be easily obtainable on the app side at
	all times. Thus, it would be nice if the FEI could answer this query.
    */
    int getFieldSize(int fieldID, int& numScalars);

    /**Since the ultimate intent for matrix-access is to bypass the FEI and go
     straight to the underlying data objects, we need a translation
     function to map between the IDs that the FEI deals in, and equation
     numbers that linear algebra objects deal in.
     @param ID Identifier of either a node or an element.
     @param idType Can take either of the values FEI_NODE or FEI_ELEMENT.
     @param fieldID Identifies a particular field at this [node||element].
     @param numEqns Output. Number of equations associated with this
     node/field (or element/field) pair.
     @param eqnNumbers Caller-allocated array. On exit, this is filled with the
     above-described equation-numbers. They are global 0-based numbers.
    */
    int getEqnNumbers(GlobalID ID,
		      int idType, 
		      int fieldID,
		      int& numEqns,
		      int* eqnNumbers);

    /**Get the solution data for a particular field, on an arbitrary set of
       nodes.
       @param fieldID Input. field identifier for which solution data is being
       requested.
       @param numNodes Input. Length of the nodeIDs list.
       @param nodeIDs Input. List specifying the nodes on which solution
       data is being requested.
       @param results Allocated by caller, but contents are output.
       Solution data for the i-th node/element starts in position i*fieldSize,
       where fieldSize is the number of scalar components that make up 
       'fieldID'.
       @return error-code 0 if successful
    */
    int getNodalFieldSolution(int fieldID,
			      int numNodes,
			      const GlobalID* nodeIDs,
			      double* results);

   /**Get the number of nodes that are local to this processor (includes nodes
      that are shared by other processors).
      @param numNodes Output. Number of local nodes.
      @return error-code 0 if successful
   */
    int getNumLocalNodes(int& numNodes);

   /**Get a list of the nodeIDs that are local to this processor (includes nodes
      that are shared by other processors).
      @param numNodes Output. Same as the value output by 'getNumLocalNodes'.
      @param nodeIDs Caller-allocated array, contents to be filled by this
      function.
      @param lenNodeIDs Input. Length of the caller-allocated nodeIDs array. If
      lenNodeIDs is less than numNodes, then only 'lenNodeIDs' nodeIDs are
      provided, of course. If lenNodeIDs is greater than numNodes, then only
      'numNodes' positions of the nodeIDs array are referenced.
      @return error-code 0 if successful
   */
    int getLocalNodeIDList(int& numNodes,
			   GlobalID* nodeIDs,
			   int lenNodeIDs);

    /** Pass nodal data for a specified field through to the solver. Example
      is geometric coordinates, etc.

    @param fieldID field identifier for the data to be passed. This is probably
       not one of the 'solution-field' identifiers. It should be an identifier
      that the solver is expecting and knows how to handle. This field id must
      previously have been provided to the fei implementation via the normal
      initFields method.

    @param numNodes number of nodes for which data is being provided
    @param nodeIDs List of length numNodes, giving node-identifiers for which
       data is being provided.
    @param nodeData List of length fieldSize*numNodes, where fieldSize is the
      size which was associated with fieldID when initFields was called. The
      data for nodeIDs[i] begins in position i*fieldSize of this array.
    */
    int putNodalFieldData(int fieldID,
			  int numNodes,
			  const GlobalID* nodeIDs,
			  const double* nodeData);

  //============================================================================
  private: //functions

    FEI_Implementation(const FEI_Implementation& src);
    FEI_Implementation& operator=(const FEI_Implementation& src);

    void deleteIDs();
    void deleteRHSScalars();

    int allocateInternalFEIs();

    void debugOut(const char* msg);
    void debugOut(const char* msg, int whichFEI);

    void buildLinearSystem();
    int aggregateSystem();

    void messageAbort(const char* msg);
    void notAllocatedAbort(const char* name);
    void needParametersAbort(const char* name);
    void badParametersAbort(const char* name);

    void setDebugOutput(const char* path, const char* name);

  //============================================================================
  private: //member variables

    fei::SharedPtr<LibraryWrapper> wrapper_;
    fei::SharedPtr<LinearSystemCore> linSysCore_;
    feiArray<fei::SharedPtr<LinearSystemCore> > lscArray_;
    bool haveLinSysCore_;
    bool haveFEData_;
    SNL_FEI_Structure* problemStructure_;
    Filter** filter_;

    snl_fei::CommUtils<int>* commUtils_;

    int numInternalFEIs_;
    bool internalFEIsAllocated_;

    feiArray<int> matrixIDs_;
    feiArray<int> numRHSIDs_;
    feiArray<int*> rhsIDs_;

    bool IDsAllocated_;

    feiArray<double> matScalars_;
    bool matScalarsSet_;
    feiArray<double*> rhsScalars_;
    bool rhsScalarsSet_;

    int index_soln_filter_;
    int index_current_filter_;
    int index_current_rhs_row_;

    int solveType_;

    bool setSolveTypeCalled_;
    bool initPhaseIsComplete_;

    bool aggregateSystemFormed_;
    int newMatrixDataLoaded_;

    Data *soln_fei_matrix_;
    Data *soln_fei_vector_;

    MPI_Comm comm_;

    int masterRank_;
    int localRank_;
    int numProcs_;

    int outputLevel_;

    int solveCounter_;
    int debugOutput_;
#ifdef FEI_HAVE_IOSFWD
    std::ostream* dbgOStreamPtr_;
#else
    ostream* dbgOStreamPtr_;
#endif
    bool dbgFileOpened_;
#ifdef FEI_HAVE_IOSFWD
    std::ofstream* dbgFStreamPtr_;
#else
    ofstream* dbgFStreamPtr_;
#endif

    double initTime_, loadTime_, solveTime_, solnReturnTime_;

    int numParams_;
    char** paramStrings_;
};

#endif

