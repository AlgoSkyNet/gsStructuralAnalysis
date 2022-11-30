/** @file benchmark_Wrinkling.cpp

    @brief Computes the wrinkling behaviour of a thin sheet

    This file is part of the G+Smo library.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.

    Author(s): H.M. Verhelst (2019-..., TU Delft)
*/

#include <gismo.h>

#include <gsKLShell/gsThinShellAssembler.h>
#include <gsKLShell/gsThinShellAssemblerDWR.h>
#include <gsKLShell/gsThinShellDWRHelper.h>
#include <gsKLShell/getMaterialMatrix.h>

// #include <gsThinShell/gsArcLengthIterator.h>
// #include <gsStructuralAnalysis/gsArcLengthIterator.h>
#include <gsAssembler/gsAdaptiveRefUtils.h>
#include <gsAssembler/gsAdaptiveMeshing.h>

#include <gsStructuralAnalysis/gsALMCrisfield.h>

using namespace gismo;

template<typename T>
class gsElementErrorPlotter : public gsFunction<T>
{
public:
    gsElementErrorPlotter(const gsBasis<T>& mp, const std::vector<T>& errors ) : m_mp(mp),m_errors(errors)
    {

    }

    virtual void eval_into(const gsMatrix<T>& u, gsMatrix<T>& res) const
    {
        // Initialize domain element iterator -- using unknown 0
        res.setZero(1,u.cols());
        for(index_t i=0; i<u.cols();++i)
        {
            int iter =0;
            // Start iteration over elements

            typename gsBasis<T>::domainIter domIt = m_mp.makeDomainIterator();
            for (; domIt->good(); domIt->next() )
            {
                 bool flag = true;
                const gsVector<T>& low = domIt->lowerCorner();
                const gsVector<T>& upp = domIt->upperCorner();


                for(int d=0; d<domainDim();++d )
                {
                    if(low(d)> u(d,i) || u(d,i) > upp(d))
                    {
                        flag = false;
                        break;
                    }
                }
                if(flag)
                {
                     res(0,i) = m_errors.at(iter);
                     break;
                }
                iter++;
            }
        }
    }

    short_t domainDim() const { return m_mp.dim();}

private:
    const gsBasis<T>& m_mp;
    const std::vector<T>& m_errors;
};

template <class T>
gsMultiPatch<T> Rectangle(T L, T B);

template <class T>
void initStepOutput( const std::string name, const gsMatrix<T> & points);

template <class T>
void writeStepOutput(const gsMatrix<T> & U, const T L, const T indicator, const gsMultiPatch<T> & deformation, const std::string name, const gsMatrix<T> & points, const index_t extreme=-1, const index_t kmax=100);

void initSectionOutput( const std::string dirname, bool undeformed=false);

template <class T>
void writeSectionOutput(const gsMultiPatch<T> & mp, const std::string dirname, const index_t coordinate=0, const T coordVal=0.0, const index_t N=100, bool undeformed=false);

template <class T>
void PlotResults(   index_t k,
                    gsThinShellAssemblerDWRBase<T> * assembler,
                    const gsMultiPatch<T> & mp, const gsMultiPatch<T> & mp_def,
                    bool plot, bool stress, bool write, bool mesh, bool deformed,
                    const std::string dirname, const std::string output,
                    gsParaviewCollection & collection,
                    gsParaviewCollection & Smembrane,
                    gsParaviewCollection & Sflexural,
                    gsParaviewCollection & Smembrane_p);

int main (int argc, char** argv)
{
    // Input options
    int numElevate  = 2;
    int numHref     = 5;
    bool plot       = false;
    bool plotError  = false;
    bool mesh = false;
    bool stress       = false;
    bool SingularPoint = false;
    bool quasiNewton = false;
    int quasiNewtonInt = -1;
    bool adaptive = false;
    bool adaptiveMesh = false;
    bool admissible = false;
    int step = 10;
    int method = 2; // (0: Load control; 1: Riks' method; 2: Crisfield's method; 3: consistent crisfield method; 4: extended iterations)
    bool symmetry = false;
    bool deformed = false;

    bool interior = true;

    real_t perturbation = 0;

    real_t tau = 1e4;

    index_t Compressibility = 0;
    index_t material = 3;
    index_t impl = 1; // 1= analytical, 2= generalized, 3= spectral

    real_t relax = 1.0;

    int result = 0;

    bool write = false;
    bool writeG = false;
    bool writeP = false;
    bool crosssection = false;

    index_t maxit = 20;

    // Arc length method options
    real_t dL = 0; // General arc length
    real_t dLb = 1e-2; // Ard length to find bifurcation
    real_t tol = 1e-6;
    real_t tolU = 1e-6;
    real_t tolF = 1e-3;

    real_t target   =1e-3;
    real_t bandwidth=1;

    // Adaptive refinement options
    index_t refExt = 0;
    index_t crsExt = 0;

    index_t markstrat = 2;
    real_t adaptRefParam = 0.9;

    std::string wn("data.csv");

    std::string dirname = "ArcLengthResults";

    std::string assemberOptionsFile("options/solver_options.xml");
    std::string mesherOptionsFile("options/mesher_options.xml");

    gsCmdLine cmd("Wrinkling analysis with thin shells.");
    cmd.addString( "o", "assemblerOpt", "Input XML file for assembler options", assemberOptionsFile );
    cmd.addString( "O", "mesherOpt", "Input XML file for mesher options", mesherOptionsFile );

    cmd.addInt("r","hRefine", "Number of dyadic h-refinement (bisection) steps to perform before solving", numHref);
    cmd.addInt("e","degreeElevation", "Number of degree elevation steps to perform on the Geometry's basis before solving", numElevate);

    cmd.addInt( "M", "Material", "Material law",  material );
    cmd.addInt( "c", "Compressibility", "1: compressible, 0: incompressible",  Compressibility );
    cmd.addInt( "I", "Implementation", "Implementation: 1= analytical, 2= generalized, 3= spectral",  impl );

    cmd.addInt("m","Method", "Arc length method; 1: Crisfield's method; 2: RIks' method.", method);
    cmd.addReal("L","dLb", "arc length", dLb);
    cmd.addReal("l","dL", "arc length after bifurcation", dL);
    cmd.addReal("A","relaxation", "Relaxation factor for arc length method", relax);

    cmd.addReal("P","perturbation", "perturbation factor", perturbation);

    cmd.addReal("F","factor", "factor for bifurcation perturbation", tau);
    cmd.addInt("q","QuasiNewtonInt","Use the Quasi Newton method every INT iterations",quasiNewtonInt);
    cmd.addInt("N", "maxsteps", "Maximum number of steps", step);

    cmd.addInt("E", "refExt", "Refinement extension", refExt);
    cmd.addInt("C", "crsExt", "Coarsening extension", crsExt);
    cmd.addReal("a", "refparam", "Controls the adaptive refinement parameter", adaptRefParam);
    cmd.addInt("u","rule", "Adaptive refinement rule; 1: ... ; 2: PUCA; 3: BULK", markstrat);
    cmd.addString("U","output", "outputDirectory", dirname);

    cmd.addReal("T","target", "Refinement target error", target);
    cmd.addReal("B","band", "Refinement target error bandwidth", bandwidth);

    cmd.addSwitch("adaptive", "Adaptive length ", adaptive);
    cmd.addSwitch("adaptiveMesh", "Adaptive mesh ", adaptiveMesh);
    cmd.addSwitch("admissible", "Admissible refinement", admissible);
    cmd.addSwitch("bifurcation", "Compute singular points and bifurcation paths", SingularPoint);
    cmd.addSwitch("quasi", "Use the Quasi Newton method", quasiNewton);
    cmd.addSwitch("plot", "Plot result in ParaView format", plot);
    cmd.addSwitch("noInterior", "Error computation not on the interior", interior);
    cmd.addSwitch("plotError", "Plot error in ParaView format", plotError);
    cmd.addSwitch("mesh", "Plot mesh?", mesh);
    cmd.addSwitch("stress", "Plot stress in ParaView format", stress);
    cmd.addSwitch("write", "Write output to file", write);
    cmd.addSwitch("writeP", "Write perturbation", writeP);
    cmd.addSwitch("writeG", "Write refined geometry", writeG);
    cmd.addSwitch("cross", "Write cross-section to file", crosssection);
    cmd.addSwitch("symmetry", "Use symmetry boundary condition (different per problem)", symmetry);
    cmd.addSwitch("deformed", "plot on deformed shape", deformed);

    try { cmd.getValues(argc,argv); } catch (int rv) { return rv; }

    MarkingStrategy adaptRefCrit;
    if (markstrat==1)
        adaptRefCrit = GARU;
    else if (markstrat==2)
        adaptRefCrit = PUCA;
    else if (markstrat==3)
        adaptRefCrit = BULK;
    else
        GISMO_ERROR("MarkingStrategy Unknown");

    if (dL==0)
    {
      dL = dLb;
    }

    real_t aDim,bDim;
    real_t thickness = 0.14e-3;
    real_t E_modulus     = 1;
    real_t PoissonRatio = 0;
    real_t Density = 1e0;
    real_t Ratio = 7.0;

    if ((!Compressibility) && (material!=0))
      PoissonRatio = 0.5;
    else
      PoissonRatio = 0.499;

    real_t mu, C01,C10;
    if (material==3)
    {
      C10 = 6.21485502e4; // c1/2
      C01 = 15.8114570e4; // c2/2
      Ratio = C10/C01;
      mu = 2*(C01+C10);
    }
    else
    {
      C10 = 19.1010178e4;
      mu = 2*C10;
    }
    E_modulus = 2*mu*(1+PoissonRatio);
    gsDebug<<"E = "<<E_modulus<<"; nu = "<<PoissonRatio<<"; mu = "<<mu<<"; ratio = "<<Ratio<<"\n";

    gsMultiPatch<> mp,mp_def;

    std::vector<boxSide> sides;
    sides.push_back(boundary::west);
    sides.push_back(boundary::east);
    if (symmetry)
      sides.push_back(boundary::south);

    bDim = 0.14; aDim = 2*bDim;
    mp.addPatch(gsNurbsCreator<>::BSplineSquare(1));
    mp.patch(0).coefs().col(0) *= aDim/2.;
    mp.patch(0).coefs().col(1) *= bDim/2.;
    mp.embed(3);

    for (index_t i = 0; i< numElevate; ++i)
        mp.patch(0).degreeElevate();    // Elevate the degree

    // Cast all patches of the mp object to THB splines
    if (adaptiveMesh)
    {
        gsMultiPatch<> mp_thb;
        gsTHBSpline<2,real_t> thb;
        for (index_t k=0; k!=mp.nPatches(); ++k)
        {
            if(gsTensorBSpline<2,real_t> *geo = dynamic_cast< gsTensorBSpline<2,real_t> * > (&mp.patch(k)))
            {
                thb = gsTHBSpline<2,real_t>(geo->basis().source(),geo->coefs());
                gsMatrix<> bbox = geo->support();
                for (index_t i = 0; i< numHref; ++i)
                    thb.refineElements(thb.basis().asElements(bbox));

                mp_thb.addPatch(thb);
            }
        }
        mp = mp_thb;
        // h-refine
    }
    else
    {
        for (index_t i = 0; i< numHref; ++i)
            mp.patch(0).uniformRefine();
    }

    mp_def = mp;

    gsInfo<<"alpha = "<<aDim/bDim<<"; beta = "<<bDim/thickness<<"\n";


    gsMultiBasis<> basisL(mp);
    gsMultiBasis<> basisH(mp);
    basisH.degreeElevate(1);
    gsInfo<<"Basis (patch 0): "<< mp.patch(0).basis() << "\n";

    // Boundary conditions
    gsBoundaryConditions<> BCs;
    BCs.setGeoMap(mp);
    gsPointLoads<real_t> pLoads = gsPointLoads<real_t>();

    std::string output = "solution";

    gsMatrix<> writePoints(2,3);
    writePoints.col(0)<< 0.0,0.5;
    writePoints.col(1)<< 0.5,0.5;
    writePoints.col(2)<< 1.0,0.5;

    gsVector<> neu(3);
    neu<<1e0/bDim,0,0;
    gsConstantFunction<> neuData(neu,3);

    BCs.addCondition(boundary::west, condition_type::dirichlet, 0, 0 ,false,0);

    BCs.addCondition(boundary::east, condition_type::collapsed, 0, 0 ,false,0);
    BCs.addCondition(boundary::east, condition_type::neumann, &neuData);
    BCs.addCondition(boundary::east, condition_type::dirichlet, 0, 0 ,false,1);
    BCs.addCondition(boundary::east, condition_type::dirichlet, 0, 0 ,false,2);

    BCs.addCondition(boundary::east, condition_type::clamped  , 0, 0, false,2);
    BCs.addCondition(boundary::west, condition_type::clamped  , 0, 0, false,2);

    BCs.addCondition(boundary::south, condition_type::dirichlet, 0, 0, false, 1 ); // unknown 2 - z.
    BCs.addCondition(boundary::south, condition_type::dirichlet, 0, 0, false, 2 ); // unknown 2 - z.

    // real_t Load = 1e0;
    // gsVector<> point(2); point<< 1.0, 0.5 ;
    // gsVector<> load (3); load << Load,0.0, 0.0;
    // pLoads.addLoad(point, load, 0 );

    dirname = dirname + "/QuarterSheet_-r" + std::to_string(numHref) + "-e" + std::to_string(numElevate) + "-M" + std::to_string(material) + "-c" + std::to_string(Compressibility) + "-alpha" + std::to_string(aDim/bDim) + "-beta" + std::to_string(bDim/thickness);

    output =  "solution";
    wn = output + "data.txt";
    SingularPoint = true;

    index_t cross_coordinate = 0;
    real_t cross_val = 0.0;


    std::string commands = "mkdir -p " + dirname;
    const char *command = commands.c_str();
    system(command);

    // plot geometry
    if (plot)
      gsWriteParaview(mp,dirname + "/" + "mp",1000,true);

    if (writeG)
    {
      gsWrite(mp,dirname + "/" + "geometry");
      gsInfo<<"Geometry written in: " + dirname + "/" + "geometry.xml\n";
    }

    if (write)
      initStepOutput(dirname + "/" + wn, writePoints);
    if (crosssection && cross_coordinate!=-1)
    {
      initSectionOutput(dirname,false); // write pointdataX.txt, pointdataY.txt, pointdataZ.txt
      initSectionOutput(dirname,true); // write pointdataX0.txt, pointdataY0.txt, pointdataZ0.txt
      writeSectionOutput(mp,dirname,cross_coordinate,cross_val,201,true);
    }
    else if (crosssection && cross_coordinate==-1)
    {
      gsInfo<<"No cross section can be exported if no coordinate is given...\n";
      crosssection=false;
    }

    gsSparseSolver<>::LU solver;

    // Linear isotropic material model
    gsFunctionExpr<> force("0","0","0",3);
    gsConstantFunction<> t(thickness,3);
    gsConstantFunction<> E(E_modulus,3);
    gsConstantFunction<> nu(PoissonRatio,3);
    gsConstantFunction<> rho(Density,3);
    gsConstantFunction<> ratio(Ratio,3);

    mu = E_modulus / (2 * (1 + PoissonRatio));
    gsConstantFunction<> alpha1(1.3,3);
    gsConstantFunction<> mu1(6.3e5/4.225e5*mu,3);
    gsConstantFunction<> alpha2(5.0,3);
    gsConstantFunction<> mu2(0.012e5/4.225e5*mu,3);
    gsConstantFunction<> alpha3(-2.0,3);
    gsConstantFunction<> mu3(-0.1e5/4.225e5*mu,3);

    std::vector<gsFunction<>*> parameters;
    if (material==0) // SvK
    {
        parameters.resize(2);
        parameters[0] = &E;
        parameters[1] = &nu;
    }
    else if (material==1 || material==2) // NH & NH_ext
    {
      parameters.resize(2);
      parameters[0] = &E;
      parameters[1] = &nu;
    }
    else if (material==3) // MR
    {
      parameters.resize(3);
      parameters[0] = &E;
      parameters[1] = &nu;
      parameters[2] = &ratio;
    }
    else if (material==4) // OG
    {
      parameters.resize(8);
      parameters[0] = &E;
      parameters[1] = &nu;
      parameters[2] = &mu1;
      parameters[3] = &alpha1;
      parameters[4] = &mu2;
      parameters[5] = &alpha2;
      parameters[6] = &mu3;
      parameters[7] = &alpha3;
    }

    gsMaterialMatrixBase<real_t>* materialMatrix;

    gsOptionList options;
    if      (material==0 && impl==1)
    {
        parameters.resize(2);
        options.addInt("Material","Material model: (0): SvK | (1): NH | (2): NH_ext | (3): MR | (4): Ogden",0);
        options.addInt("Implementation","Implementation: (0): Composites | (1): Analytical | (2): Generalized | (3): Spectral",1);
        materialMatrix = getMaterialMatrix<3,real_t>(mp,t,parameters,rho,options);
    }
    else
    {
        options.addInt("Material","Material model: (0): SvK | (1): NH | (2): NH_ext | (3): MR | (4): Ogden",material);
        options.addSwitch("Compressibility","Compressibility: (false): Imcompressible | (true): Compressible",Compressibility);
        options.addInt("Implementation","Implementation: (0): Composites | (1): Analytical | (2): Generalized | (3): Spectral",impl);
        materialMatrix = getMaterialMatrix<3,real_t>(mp,t,parameters,rho,options);
    }

    gsThinShellAssemblerDWRBase<real_t>* assembler;
    assembler = new gsThinShellAssemblerDWR<3, real_t, true >(mp,basisL,basisH,BCs,force,materialMatrix);
    assembler->setGoal(GoalFunction::PStress,0);

    // Construct assembler object
    gsFileData<> fd_assembler(assemberOptionsFile);
    gsOptionList assemblerOpts;
    fd_assembler.getFirst<gsOptionList>(assemblerOpts);
    assembler->setOptions(assemblerOpts);
    assembler->setPointLoads(pLoads);

    gsStopwatch stopwatch;
    real_t time = 0.0;

    typedef std::function<gsSparseMatrix<real_t> (gsVector<real_t> const &)>                                Jacobian_t;
    typedef std::function<gsVector<real_t> (gsVector<real_t> const &, real_t, gsVector<real_t> const &) >   ALResidual_t;
    // Function for the Jacobian
    Jacobian_t Jacobian = [&time,&stopwatch,&assembler](gsVector<real_t> const &x)
    {
        gsMultiPatch<> def;
        stopwatch.restart();
        assembler->constructSolutionL(x,def);
        assembler->assembleMatrixL(def);
        time += stopwatch.stop();

        gsSparseMatrix<real_t> m = assembler->matrixL();
        return m;
    };
    // Function for the Residual
    ALResidual_t ALResidual = [&time,&stopwatch,&assembler](gsVector<real_t> const &x, real_t lam, gsVector<real_t> const &force)
    {
        gsMultiPatch<> def;
        stopwatch.restart();
        assembler->constructSolutionL(x,def);
        assembler->assemblePrimalL(def);
        gsVector<real_t> Fint = -(assembler->primalL() - force);
        gsVector<real_t> result = Fint - lam * force;
        time += stopwatch.stop();
        return result; // - lam * force;
    };
    // Assemble linear system to obtain the force vector
    assembler->assembleL();
    gsVector<> Force = assembler->primalL();


    gsParaviewCollection collection(dirname + "/" + output);
    gsParaviewCollection Smembrane(dirname + "/" + "membrane");
    gsParaviewCollection Sflexural(dirname + "/" + "flexural");
    gsParaviewCollection Smembrane_p(dirname + "/" + "membrane_p");
    gsMultiPatch<> deformation = mp;

    // Make objects for previous solutions
    real_t Lold = 0, deltaLold = 0;
    real_t L = 0, deltaL = 0;
    gsMatrix<> U(Force.size(),1), deltaU(Force.size(),1);
    U.setZero();
    deltaU.setZero();
    gsMatrix<> Uold(Force.size(),1), deltaUold(Force.size(),1);
    Uold.setZero();
    deltaUold.setZero();

    gsMatrix<> solVector;
    real_t indicator_prev = 0.0;
    real_t indicator = 0.0;
    bool bisected = false;
    bool unstable_prev = false;
    real_t dLb0 = dLb;

    gsFileData<> fd_mesher(mesherOptionsFile);
    gsOptionList mesherOpts;
    fd_mesher.getFirst<gsOptionList>(mesherOpts);
    gsAdaptiveMeshing<real_t> mesher;
    if (adaptiveMesh)
    {
        mesher = gsAdaptiveMeshing<real_t>(mp);
        mesher.options() = mesherOpts;
        mesher.getOptions();
    }

    gsHBoxContainer<2,real_t> markRef, markCrs;

    gsMultiPatch<> U_patch, deltaU_patch;
    gsMultiPatch<> Uold_patch, deltaUold_patch;

    assembler->constructMultiPatchL(Uold,Uold_patch);
    assembler->constructMultiPatchL(deltaUold,deltaUold_patch);

    std::vector<std::vector<std::pair<index_t,real_t>>> write_errors; // per load step, iteration, numDoFs, error
    std::vector<std::pair<index_t,real_t>> loadstep_errors;

    gsALMCrisfield<real_t> arcLength(Jacobian, ALResidual, Force);
    gsOptionList ALMoptions = arcLength.options();

#ifdef GISMO_WITH_PARDISO
    ALMoptions.setString("Solver","PardisoLU"); // LDLT solver
#else
    ALMoptions.setString("Solver","SimplicialLDLT"); // LDLT solver
#endif
    ALMoptions.setInt("BifurcationMethod",0); // 0: determinant, 1: eigenvalue
    ALMoptions.setReal("Length",dLb);
    ALMoptions.setInt("AngleMethod",0); // 0: step, 1: iteration
    ALMoptions.setSwitch("AdaptiveLength",adaptive);
    ALMoptions.setInt("AdaptiveIterations",5);
    ALMoptions.setReal("Perturbation",tau);
    ALMoptions.setReal("Scaling",0.0);
    ALMoptions.setReal("Tol",tol);
    ALMoptions.setReal("TolU",tolU);
    ALMoptions.setReal("TolF",tolF);
    ALMoptions.setInt("MaxIter",maxit);
    ALMoptions.setSwitch("Verbose",true);
    ALMoptions.setReal("Relaxation",relax);
    if (quasiNewtonInt>0)
    {
      quasiNewton = true;
      ALMoptions.setInt("QuasiIterations",quasiNewtonInt);
    }
    ALMoptions.setSwitch("Quasi",quasiNewton);

    gsInfo<<ALMoptions;

    arcLength.options() = ALMoptions;
    arcLength.applyOptions();
    arcLength.initialize();

    gsThinShellDWRHelper<real_t> helper(assembler);
    typename gsBoxTopology::bContainer goalSides;
    //goalSides.push_back(patchSide(0,boundary::west));
    gsMatrix<> points;
    real_t error = 1;

    // PRE-BUCKLING
    bool unstable = false;
    index_t k = 0;
    gsInfo<<"----------Pre-Buckling-----------\n";
    for ( ; k<step; k++)
    {
        loadstep_errors.clear();
        gsInfo<<"Load step "<< k<<"; \tSystem size = "<<Uold.size()<<" x "<<Uold.size()<<"\n";
        gsParaviewCollection errors(dirname + "/" + "error" + util::to_string(k));
        gsParaviewCollection error_fields(dirname + "/" + "error_field" + util::to_string(k));

        arcLength.setLength(dLb);

        gsInfo<<"Starting from U.norm()="<<Uold.norm()<<", L="<<Lold<<"\n";
        arcLength.step();

        if (!(arcLength.converged()))
        {
          gsInfo<<"Error: Loop terminated, arc length method did not converge.\n";
          dLb = dLb / 2.;
          arcLength.setLength(dLb);
          arcLength.setSolution(Uold,Lold);
          bisected = true;
          k -= 1;
          continue;
        }
        indicator = arcLength.indicator();
        gsInfo<<"indicator: (old = )"<<indicator_prev<<"; (new = )"<<indicator<<"\n";

        arcLength.computeStability(arcLength.solutionU(),quasiNewton);
        unstable = arcLength.stabilityChange();

        if (unstable)
            break;

        L = Lold = arcLength.solutionL();
        deltaL = deltaLold = arcLength.solutionDL();
        U = Uold = arcLength.solutionU();
        deltaU = deltaUold = arcLength.solutionDU();

        // Deformed geometry
        assembler->constructSolutionL(U,mp_def);
        // Deformation (primal)
        assembler->constructMultiPatchL(U,U_patch);
        // delta Deformation
        assembler->constructMultiPatchL(U,deltaU_patch);

        // Update Uold
        Uold_patch = U_patch;
        deltaUold_patch = deltaU_patch;

        indicator_prev = indicator;

        ///////////////////////////////////////////////////
        index_t it = 0;
        if (plot)
        {
            std::string fileName = dirname + "/" + "error_field" + util::to_string(k) + "_" + util::to_string(it);
            helper.computeError(mp_def,U_patch,goalSides,points,interior,fileName,1000,false,mesh);
            fileName = "error_field" + util::to_string(k) + "_" + util::to_string(it) ;
            for (size_t p=0; p!=mp.nPatches(); p++)
            {
                error_fields.addTimestep(fileName+std::to_string(p),it,".vts");
                if (mesh)
                    error_fields.addTimestep(fileName + "_mesh"+std::to_string(p),it,".vtp");
            }
        }
        else
            helper.computeError(mp_def,U_patch,goalSides,points,interior);

        error = std::abs(helper.error());

        gsInfo<<"Error = "<<error<<"\n";
        loadstep_errors.push_back(std::make_pair(assembler->numDofsL(),error));
        ///////////////////////////////////////////////////

        deformation = mp_def;
        for (index_t p=0; p!=mp_def.nPatches(); p++)
	    deformation.patch(p).coefs() -= mp.patch(p).coefs();


        PlotResults(k,assembler,mp,mp_def,plot,stress,write,mesh,deformed,dirname,output,
                    collection,Smembrane,Sflexural,Smembrane_p);

        if (write)
            writeStepOutput(U,L,indicator,deformation, dirname + "/" + wn, writePoints,1, 201);

        if (crosssection && cross_coordinate!=-1)
            writeSectionOutput(deformation,dirname,cross_coordinate,cross_val,201,false);

        write_errors.push_back(loadstep_errors);
    }

    // BUCKLING
    gsInfo<<"----------Buckling mode computation-----------\n";
    if (unstable)
    {
        loadstep_errors.clear();
        gsInfo<<"Bifurcation spotted!"<<"\n";
        arcLength.computeSingularPoint(1e-4, 5, Uold, Lold, 1e-7, 0, false);
        arcLength.switchBranch();
        dLb0 = dLb = dL;
        arcLength.setLength(dLb);

        if (writeP)
        {
            gsMultiPatch<> mp_perturbation;
            assembler->constructSolutionL(arcLength.solutionV(),mp_perturbation);
            gsWrite(mp_perturbation,dirname + "/" +"perturbation");
            gsInfo<<"Perturbation written in: " + dirname + "/" + "perturbation.xml\n";
        }
        indicator = 0;

        L = Lold = arcLength.solutionL();
        deltaL = deltaLold = arcLength.solutionDL();
        U = Uold = arcLength.solutionU();
        deltaU = deltaUold = arcLength.solutionDU();

        // Deformed geometry
        assembler->constructSolutionL(U,mp_def);
        // Deformation (primal)
        assembler->constructMultiPatchL(U,U_patch);
        // delta Deformation
        assembler->constructMultiPatchL(U,deltaU_patch);

        // Update Uold
        Uold_patch = U_patch;
        deltaUold_patch = deltaU_patch;

        deformation = mp_def;
        for (index_t p=0; p!=mp_def.nPatches(); p++)
            deformation.patch(p).coefs() -= mp.patch(p).coefs();

        PlotResults(k,assembler,mp,mp_def,plot,stress,write,mesh,deformed,dirname,output,
                    collection,Smembrane,Sflexural,Smembrane_p);

        if (write)
            writeStepOutput(U,L,indicator,deformation, dirname + "/" + wn, writePoints,1, 201);

        if (crosssection && cross_coordinate!=-1)
            writeSectionOutput(deformation,dirname,cross_coordinate,cross_val,201,false);

        loadstep_errors.push_back(std::make_pair(-1,-1.));
        write_errors.push_back(loadstep_errors);
        unstable = false;
        unstable_prev = true;
    }

    if (adaptiveMesh)
    {
        gsMatrix<> coefs;

        gsQuasiInterpolate<real_t>::localIntpl(basisL.basis(0), mp_def.patch(0), coefs);
        mp_def.patch(0) = *basisL.basis(0).makeGeometry(give(coefs));

        gsQuasiInterpolate<real_t>::localIntpl(basisL.basis(0), U_patch.patch(0), coefs);
        U_patch.patch(0) = *basisL.basis(0).makeGeometry(give(coefs));

        gsQuasiInterpolate<real_t>::localIntpl(basisL.basis(0), deltaU_patch.patch(0), coefs);
        deltaU_patch.patch(0) = *basisL.basis(0).makeGeometry(give(coefs));

        gsQuasiInterpolate<real_t>::localIntpl(basisL.basis(0), Uold_patch.patch(0), coefs);
        Uold_patch.patch(0) = *basisL.basis(0).makeGeometry(give(coefs));

        gsQuasiInterpolate<real_t>::localIntpl(basisL.basis(0), deltaUold_patch.patch(0), coefs);
        deltaUold_patch.patch(0) = *basisL.basis(0).makeGeometry(give(coefs));
    }

    gsInfo<<"----------Post-Buckling-----------\n";
    // POST BUCKLING
    real_t refTol = target / bandwidth; // refine if error is above
    real_t crsTol = target * bandwidth; // coarsen if error is below
    GISMO_ENSURE(refTol >= crsTol,"Refinement tolerance should be bigger than the coarsen tolerance");
    for ( ; k<step; k++)
    {
        loadstep_errors.clear();
        gsInfo<<"Load step "<< k<<"; \tSystem size = "<<Uold.size()<<" x "<<Uold.size()<<"\n";
        gsParaviewCollection errors(dirname + "/" + "error" + util::to_string(k));
        gsParaviewCollection error_fields(dirname + "/" + "error_field" + util::to_string(k));

        gsInfo<<"Basis (L): \n"<<mp.basis(0)<<"\n";
        index_t maxIt = 10;
        index_t it = 0;
        bool refined = true;
        bool coarsened = true;
        error = 1;
        bool bandtest = (bandwidth==1) ? error > refTol : ((error < crsTol )|| (error >= refTol));
        while (bandtest && it < maxIt && (refined || coarsened))
        {
            assembler->assembleL();
            Force = assembler->primalL();
            Uold = assembler->constructSolutionVectorL(Uold_patch);
            deltaUold = assembler->constructSolutionVectorL(deltaUold_patch);

            gsALMCrisfield<real_t> arcLength(Jacobian, ALResidual, Force);
            arcLength.options() = ALMoptions;
            arcLength.applyOptions();
            arcLength.initialize();
            arcLength.setIndicator(indicator); // RESET INDICATOR
            arcLength.setSolution(Uold,Lold);
            arcLength.setSolutionStep(deltaUold,deltaLold);
            arcLength.setLength(dLb);

            gsInfo<<"Starting from U.norm()="<<Uold.norm()<<", L="<<Lold<<"\n";
            arcLength.step();

            if (!(arcLength.converged()))
            {
              gsInfo<<"Error: Loop terminated, arc length method did not converge.\n";
              dLb = dLb / 2.;
              arcLength.setLength(dLb);
              arcLength.setSolution(Uold,Lold);
              bisected = true;
              it -= 1;
              continue;
            }
            indicator = arcLength.indicator();
            gsInfo<<"indicator: (old = )"<<indicator_prev<<"; (new = )"<<indicator<<"\n";

            L = arcLength.solutionL();
            deltaL = arcLength.solutionDL();
            U = arcLength.solutionU();
            deltaU = arcLength.solutionDU();

            // Deformed geometry
            assembler->constructSolutionL(U,mp_def);
            // Deformation (primal)
            assembler->constructMultiPatchL(U,U_patch);
            // delta Deformation
            assembler->constructMultiPatchL(U,deltaU_patch);

            /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // ERROR ESTIMATION PART
            /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            if (plot)
            {
                std::string fileName = dirname + "/" + "error_field" + util::to_string(k) + "_" + util::to_string(it);
                helper.computeError(mp_def,U_patch,goalSides,points,interior,fileName,1000,false,mesh);
                fileName = "error_field" + util::to_string(k) + "_" + util::to_string(it) ;
                for (size_t p=0; p!=mp.nPatches(); p++)
                {
                    error_fields.addTimestep(fileName+std::to_string(p),it,".vts");
                    if (mesh)
                        error_fields.addTimestep(fileName + "_mesh"+std::to_string(p),it,".vtp");
                }
            }
            else
                helper.computeError(mp_def,U_patch,goalSides,points,interior);

            error = std::abs(helper.error());
            gsInfo<<"Error = "<<error<<"\n";
            loadstep_errors.push_back(std::make_pair(assembler->numDofsL(),error));

            std::vector<real_t> errorVec = helper.errors();
            std::vector<real_t> elErrors = helper.absErrors();

            if (plotError)
            {
                for (size_t p=0; p!=mp.nPatches(); p++)
                {
                    gsElementErrorPlotter<real_t> err_eh(mp.basis(p),elErrors);
                    const gsField<> elemError_eh( mp.patch(p), err_eh, true );
                    std::string fileName = dirname + "/" + "error" + util::to_string(k) + "_" + util::to_string(it);
                    writeSinglePatchField<>(mp.patch(p), err_eh, true, fileName + "_" + util::to_string(p), 1000);
                    if (mesh)
                        writeSingleCompMesh<>(mp.basis(p), mp.patch(p),fileName + "_mesh" + "_" + util::to_string(p));
                    fileName = "error" + util::to_string(k) + "_" + util::to_string(it) ;
                    errors.addTimestep(fileName,p,it,".vts");
                    if (mesh)
                        errors.addTimestep(fileName + "_mesh",p,it,".vtp");
                }
            }

            /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // ADAPTIVE MESHING PART
            /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            if (adaptiveMesh)
            {
                if (unstable_prev)
                {
                    unstable_prev = false;
                    break;
                }
                else
                {
                    if (error > refTol)
                    {
                        gsInfo<<"Load Step "<<k<<": Error is too big! Error = "<<error<<", refTol = "<<refTol<<"\n";
                        mesher.markRef_into(elErrors,markRef);
                        gsInfo<<"Marked "<<markRef.totalSize()<<" elements for refinement\n";
                        refined = mesher.refine(markRef);
                    }
                    else if (error < refTol && error > crsTol)
                    {
                        gsInfo<<"Load Step "<<k<<": Error is within bounds. Error = "<<error<<", refTol = "<<refTol<<", crsTol = "<<crsTol<<"\n";
                        // mesher.markRef_into(elErrors,markRef);
                        // gsInfo<<"Marked "<<markRef.totalSize()<<" elements for refinement\n";
                        // gsInfo<<"Marked "<<markCrs.totalSize()<<" elements for coarsening\n";
                        // mesher.markCrs_into(elErrors,markRef,markCrs);
                        // refined = mesher.refine(markRef);
                        gsInfo<<"No elements marked\n";
                    }
                    else if (error < crsTol)
                    {
                        //gsInfo<<"Error is too small!\n";
                        gsInfo<<"Load Step "<<k<<": Error is too small! Error = "<<error<<", crsTol = "<<crsTol<<"\n";
                        mesher.markCrs_into(elErrors,markCrs);
                        gsInfo<<"Marked "<<markCrs.totalSize()<<" elements for coarsening\n";
                        coarsened = mesher.unrefine(markCrs);
                    }
                    bandtest = (bandwidth==1) ? error > refTol : ((error < crsTol )|| (error >= refTol));

                    basisL = gsMultiBasis<>(mp);
                    basisH = basisL;
                    basisH.degreeElevate(1);

                    // Project the solution from old mesh to new mesh
                    gsMatrix<> coefs;

                    // Which of those are needed?

                    gsQuasiInterpolate<real_t>::localIntpl(basisL.basis(0), mp_def.patch(0), coefs);
                    mp_def.patch(0) = *basisL.basis(0).makeGeometry(give(coefs));

                    gsQuasiInterpolate<real_t>::localIntpl(basisL.basis(0), U_patch.patch(0), coefs);
                    U_patch.patch(0) = *basisL.basis(0).makeGeometry(give(coefs));

                    gsQuasiInterpolate<real_t>::localIntpl(basisL.basis(0), deltaU_patch.patch(0), coefs);
                    deltaU_patch.patch(0) = *basisL.basis(0).makeGeometry(give(coefs));

                    gsQuasiInterpolate<real_t>::localIntpl(basisL.basis(0), Uold_patch.patch(0), coefs);
                    Uold_patch.patch(0) = *basisL.basis(0).makeGeometry(give(coefs));

                    gsQuasiInterpolate<real_t>::localIntpl(basisL.basis(0), deltaUold_patch.patch(0), coefs);
                    deltaUold_patch.patch(0) = *basisL.basis(0).makeGeometry(give(coefs));

                    assembler->setBasisL(basisL);
                    assembler->setBasisH(basisH);
                    assembler->setUndeformed(mp);

                    mesher.rebuild();

                    // assembler->constructSolutionL(U,mp_def);
                    unstable_prev = false;

                }
                it++;
            }
            else
                break;
        }

        if (plotError)
        {
            errors.save();
            error_fields.save();
        }

        // Update Uold
        Uold_patch = U_patch;
        deltaUold_patch = deltaU_patch;
        Lold = L;
        deltaLold = deltaL;

        // not needed??
        // Uold = U;
        // Lold = L;
        // deltaUold = deltaU;
        // deltaLold = deltaL;

        indicator_prev = indicator;

        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//gsInfo<<"deformation.patch(0).coefs().col(2) = "<<deformation.patch(0).coefs().col(2)<<"\n";
        deformation = mp_def;
        for (index_t p=0; p!=mp_def.nPatches(); p++)
            deformation.patch(p).coefs() -= mp.patch(p).coefs();

        PlotResults(k,assembler,mp,mp_def,plot,stress,write,mesh,deformed,dirname,output,
                    collection,Smembrane,Sflexural,Smembrane_p);

        if (write)
            writeStepOutput(U,L,indicator,deformation, dirname + "/" + wn, writePoints,1, 201);

        if (crosssection && cross_coordinate!=-1)
            writeSectionOutput(deformation,dirname,cross_coordinate,cross_val,201,false);

        write_errors.push_back(loadstep_errors);
    }

    if (plot)
    {
      collection.save();
    }
    if (stress)
    {
      Smembrane.save();
      Sflexural.save();
      Smembrane_p.save();
    }

    std::ofstream file;
    file.open(dirname + "/" + "errors.csv",std::ofstream::out);
    index_t loadstep=0;
    file<<"load_step,iteration,numDofs,error\n";
    for (std::vector<std::vector<std::pair<index_t,real_t>>>::const_iterator it = write_errors.begin(); it!=write_errors.end(); it++, loadstep++)
    {
        index_t iteration=0;
        for (std::vector<std::pair<index_t,real_t>>::const_iterator iit = it->begin(); iit!=it->end(); iit++, iteration++)
            file<<loadstep<<","<<iteration<<","<<iit->first<<","<<iit->second<<"\n";

    }
    file.close();

  return result;
}


template <class T>
gsMultiPatch<T> Rectangle(T L, T B) //, int n, int m, std::vector<boxSide> sides, T offset)
{
  // -------------------------------------------------------------------------
  // --------------------------Make beam geometry-----------------------------
  // -------------------------------------------------------------------------
  int dim = 3; //physical dimension
  gsKnotVector<> kv0;
  kv0.initUniform(0,1,0,2,1);
  gsKnotVector<> kv1;
  kv1.initUniform(0,1,0,2,1);

  // Make basis
  gsTensorBSplineBasis<2,T> basis(kv0,kv1);

  // Initiate coefficient matrix
  gsMatrix<> coefs(basis.size(),dim);
  // Number of control points needed per component
  size_t len0 = basis.component(0).size();
  size_t len1 = basis.component(1).size();
  gsVector<> coefvec0(len0);
  // Uniformly distribute control points per component
  coefvec0.setLinSpaced(len0,0.0,L);
  gsVector<> coefvec1(basis.component(1).size());
  coefvec1.setLinSpaced(len1,0.0,B);

  // Z coordinate is zero
  coefs.col(2).setZero();

  // Define a matrix with ones
  gsVector<> temp(len0);
  temp.setOnes();
  for (size_t k = 0; k < len1; k++)
  {
    // First column contains x-coordinates (length)
    coefs.col(0).segment(k*len0,len0) = coefvec0;
    // Second column contains y-coordinates (width)
    coefs.col(1).segment(k*len0,len0) = temp*coefvec1.at(k);
  }
  // Create gsGeometry-derived object for the patch
  gsTensorBSpline<2,real_t> shape(basis,coefs);

  gsMultiPatch<T> mp;
  mp.addPatch(shape);
  mp.addAutoBoundaries();

  return mp;
}


template <class T>
void initStepOutput(const std::string name, const gsMatrix<T> & points)
{
  std::ofstream file;
  file.open(name,std::ofstream::out);
  file  << std::setprecision(20)
        << "Deformation norm" << ",";
        for (index_t k=0; k!=points.cols(); k++)
        {
          file<< "point "<<k<<" - x" << ","
              << "point "<<k<<" - y" << ","
              << "point "<<k<<" - z" << ",";
        }

  file  << "Lambda" << ","
        << "Indicator"
        << "\n";
  file.close();

  gsInfo<<"Step results will be written in file: "<<name<<"\n";
}

template <class T>
void writeStepOutput(const gsMatrix<T> & U, const T L, const T indicator, const gsMultiPatch<T> & deformation, const std::string name, const gsMatrix<T> & points, const index_t extreme, const index_t kmax) // extreme: the column of point indices to compute the extreme over (default -1)
{
  gsMatrix<T> P(2,1), Q(2,1);
  gsMatrix<T> out(3,points.cols());
  gsMatrix<T> tmp;

  for (index_t p=0; p!=points.cols(); p++)
  {
    P<<points.col(p);
    deformation.patch(0).eval_into(P,tmp);
    out.col(p) = tmp;
  }

  std::ofstream file;
  file.open(name,std::ofstream::out | std::ofstream::app);
  if (extreme==-1)
  {
    file  << std::setprecision(6)
          << U.norm() << ",";
          for (index_t p=0; p!=points.cols(); p++)
          {
            file<< out(0,p) << ","
                << out(1,p) << ","
                << out(2,p) << ",";
          }

    file  << L << ","
          << indicator << ","
          << "\n";
  }
  else if (extreme==0 || extreme==1)
  {
	gsInfo<<"kmax = "<<kmax<<"\n";
    gsMatrix<T> out2(kmax,points.cols()); // evaluation points in the rows, output (per coordinate) in columns
    for (int p = 0; p != points.cols(); p ++)
    {
      Q.at(1-extreme) = points(1-extreme,p);
      for (int k = 0; k != kmax; k ++)
      {
        Q.at(extreme) = 1.0*k/(kmax-1);
        deformation.patch(0).eval_into(Q,tmp);
        out2(k,p) = tmp.at(2); // z coordinate
      }
    }

    file  << std::setprecision(6)
          << U.norm() << ",";
          for (index_t p=0; p!=points.cols(); p++)
          {
		gsInfo<<"out = "<<out<<"\n";
            file<< out(0,p) << ","
                << out(1,p) << ","
                << std::max(abs(out2.col(p).maxCoeff()),abs(out2.col(p).minCoeff())) << ",";
          }

    file  << L << ","
          << indicator << ","
          << "\n";
  }
  else
    GISMO_ERROR("Extremes setting unknown");

  file.close();
}

void initSectionOutput(const std::string dirname, bool undeformed)
{
  std::ofstream file2, file3, file4;
  std::string wn2,wn3,wn4;

  if (! undeformed)
  {
    wn2 = dirname + "/" + "pointdataX.txt";
    wn3 = dirname + "/" + "pointdataY.txt";
    wn4 = dirname + "/" + "pointdataZ.txt";
  }
  else
  {
    wn2 = dirname + "/" + "pointdataX0.txt";
    wn3 = dirname + "/" + "pointdataY0.txt";
    wn4 = dirname + "/" + "pointdataZ0.txt";
  }

  file2.open(wn2,std::ofstream::out);
  file2.close();

  file3.open(wn3,std::ofstream::out);
  file3.close();

  file4.open(wn4,std::ofstream::out);
  file4.close();

  gsInfo<<"Cross-section results will be written in directory: "<<dirname<<"\n";
}

template <class T>
void writeSectionOutput(const gsMultiPatch<T> & mp, const std::string dirname, const index_t coordinate, const T coordVal, const index_t N, bool undeformed) // coordinate: the column which remains constant at coordVal
{
  gsMatrix<T> P(2,1);
  gsMatrix<T> tmp;
  P.setZero();
  P.at(coordinate) = coordVal;

  std::ofstream file2, file3, file4;
  std::string wn2,wn3,wn4;

  if (! undeformed)
  {
    wn2 = dirname + "/" + "pointdataX.txt";
    wn3 = dirname + "/" + "pointdataY.txt";
    wn4 = dirname + "/" + "pointdataZ.txt";
  }
  else
  {
    wn2 = dirname + "/" + "pointdataX0.txt";
    wn3 = dirname + "/" + "pointdataY0.txt";
    wn4 = dirname + "/" + "pointdataZ0.txt";
  }

  file2.open(wn2,std::ofstream::out | std::ofstream::app);
  file3.open(wn3,std::ofstream::out | std::ofstream::app);
  file4.open(wn4,std::ofstream::out | std::ofstream::app);


  gsMatrix<T> out(3,N); // evaluation points in the rows, output (per coordinate) in columns
    for (int k = 0; k != N; k ++)
    {
      P.at(1-coordinate) = 1.0*k/(N-1);

      mp.patch(0).eval_into(P,tmp);
      out.col(k) = tmp; // z coordinate

      std::string str2 = std::to_string(out(0,k));
      std::string str3 = std::to_string(out(1,k));
      std::string str4 = std::to_string(out(2,k));
      if(k+1 == N)
      {
          file2<<str2;
          file3<<str3;
          file4<<str4;
      }
      else{
          file2<<str2<<',';
          file3<<str3<<',';
          file4<<str4<<',';
      }
    }
    file2<<'\n';
    file2.close();
    file3<<'\n';
    file3.close();
    file4<<'\n';
    file4.close();
}

template <class T>
void PlotResults(   index_t k,
                    gsThinShellAssemblerDWRBase<T> * assembler,
                    const gsMultiPatch<T> & mp, const gsMultiPatch<T> & mp_def,
                    bool plot, bool stress, bool write, bool mesh, bool deformed,
                    const std::string dirname, const std::string output,
                    gsParaviewCollection & collection,
                    gsParaviewCollection & Smembrane,
                    gsParaviewCollection & Sflexural,
                    gsParaviewCollection & Smembrane_p)
{
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    gsMultiPatch<T> deformation = mp_def;

    deformation.patch(0).coefs() -= mp.patch(0).coefs();// assuming 1 patch here

    gsInfo<<"Total ellapsed assembly time: "<<time<<" s\n";

    if (plot)
    {
        gsField<T> solField;
        if (deformed)
          solField= gsField<>(mp_def,deformation);
        else
          solField= gsField<>(mp,deformation);

        std::string fileName = dirname + "/" + output + util::to_string(k);
        gsWriteParaview<T>(solField, fileName, 1000,mesh);
        fileName = output + util::to_string(k) + "0";
        collection.addTimestep(fileName,k,".vts");
        if (mesh) collection.addTimestep(fileName,k,"_mesh.vtp");
    }
    if (stress)
    {
        gsField<T> membraneStress, flexuralStress, membraneStress_p;

        gsPiecewiseFunction<T> membraneStresses;
        assembler->constructStress(mp_def,membraneStresses,stress_type::membrane);
        if (deformed)
          membraneStress = gsField<>(mp_def,membraneStresses,true);
        else
          membraneStress = gsField<>(mp,membraneStresses,true);

        gsPiecewiseFunction<T> flexuralStresses;
        assembler->constructStress(mp_def,flexuralStresses,stress_type::flexural);
        if (deformed)
          flexuralStress = gsField<>(mp_def,flexuralStresses, true);
        else
          flexuralStress = gsField<>(mp,flexuralStresses, true);

        gsPiecewiseFunction<T> membraneStresses_p;
        assembler->constructStress(mp_def,membraneStresses_p,stress_type::principal_stress);
        if (deformed)
          membraneStress_p = gsField<>(mp_def,membraneStresses_p, true);
        else
          membraneStress_p = gsField<>(mp,membraneStresses_p, true);

        std::string fileName;
        fileName = dirname + "/" + "membrane" + util::to_string(k);
        gsWriteParaview( membraneStress, fileName, 1000);
        fileName = "membrane" + util::to_string(k) + "0";
        Smembrane.addTimestep(fileName,k,".vts");

        fileName = dirname + "/" + "flexural" + util::to_string(k);
        gsWriteParaview( flexuralStress, fileName, 1000);
        fileName = "flexural" + util::to_string(k) + "0";
        Sflexural.addTimestep(fileName,k,".vts");

        fileName = dirname + "/" + "membrane_p" + util::to_string(k);
        gsWriteParaview( membraneStress_p, fileName, 1000);
        fileName = "membrane_p" + util::to_string(k) + "0";
        Smembrane_p.addTimestep(fileName,k,".vts");
    }
}
