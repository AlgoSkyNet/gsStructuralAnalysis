
/** @file gsThinShell_BucklingArcLength.cpp

    @brief Code for the arc-length method of a shell based on loads

    This file is part of the G+Smo library.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.

    Author(s): H.M. Verhelst
*/

#include <gismo.h>

#include <gsKLShell/gsThinShellAssembler.h>
#include <gsKLShell/getMaterialMatrix.h>
// #include <gsThinShell/gsArcLengthIterator.h>
#include <gsStructuralAnalysis/gsArcLengthIterator.h>

using namespace gismo;

int main (int argc, char** argv)
{
    // Input options
    int numElevate    = 0;
    int numRefine     = 1;
    bool plot         = false;
    bool stress       = false;
    bool quasiNewton  = false;
    int quasiNewtonInt= -1;
    bool adaptive     = false;
    int step          = 21;
    int method        = 2; // (0: Load control; 1: Riks' method; 2: Crisfield's method; 3: consistent crisfield method; 4: extended iterations)


    index_t Compressibility = 0;
    index_t material  = 0;
    bool composite = false;
    index_t impl = 1; // 1= analytical, 2= generalized, 3= spectral

    real_t relax      = 1.0;
    int result        = 0;
    index_t maxit     = 50;
    // Arc length method options
    real_t dL         = 1e0; // General arc length
    real_t tol        = 1e-6;
    real_t tolU       = 1e-6;
    real_t tolF       = 1e-3;

    std::string wn("data.csv");

    gsCmdLine cmd("Arc-length analysis for thin shells.");

    cmd.addInt("r","hRefine", "Number of dyadic h-refinement (bisection) steps to perform before solving", numRefine);
    cmd.addInt("e","degreeElevation", "Number of degree elevation steps to perform on the Geometry's basis before solving", numElevate);
    cmd.addInt( "M", "Material", "Material law",  material );
    cmd.addInt( "c", "Compressibility", "1: compressible, 0: incompressible",  Compressibility );
    cmd.addInt( "I", "Implementation", "Implementation: 1= analytical, 2= generalized, 3= spectral",  impl );
    cmd.addSwitch("composite", "Composite material", composite);

    cmd.addInt("m","Method", "Arc length method; 1: Crisfield's method; 2: RIks' method.", method);
    cmd.addReal("L","dL", "arc length", dL);
    cmd.addReal("A","relaxation", "Relaxation factor for arc length method", relax);

    cmd.addInt("q","QuasiNewtonInt","Use the Quasi Newton method every INT iterations",quasiNewtonInt);
    cmd.addInt("N", "maxsteps", "Maximum number of steps", step);

    cmd.addSwitch("adaptive", "Adaptive length ", adaptive);
    cmd.addSwitch("quasi", "Use the Quasi Newton method", quasiNewton);
    cmd.addSwitch("plot", "Plot result in ParaView format", plot);
    cmd.addSwitch("stress", "Plot stress in ParaView format", stress);

    try { cmd.getValues(argc,argv); } catch (int rv) { return rv; }

    /*
      Uniaxial tension of a square plate                            --- Validation settings: -L 1eX -l 1eX -M 14 -N 500 -r X -e X
      (bottom boundary fixed in Y, left boundary fixed in X, right boundary normal load)
    */
    real_t mu = 4.225e5;
    real_t thickness = 0.1;
    real_t PoissonRatio;
    if (!Compressibility)
      PoissonRatio = 0.5;
    else
      PoissonRatio = 0.45;
    real_t E_modulus = 2*mu*(1+PoissonRatio);
    real_t Density    = 1e0;
    real_t Ratio      = 7.0;

    gsMultiPatch<> mp,mp_def;
    gsReadFile<>("surface/eighth_sphere.xml", mp);

    // p-refine
    for(index_t i = 0; i< numElevate; ++i)
      mp.patch(0).degreeElevate();    // Elevate the degree

    // h-refine
    for(index_t i = 0; i< numRefine; ++i)
      mp.patch(0).uniformRefine();

    gsInfo<<"mu = "<<E_modulus / (2 * (1 + PoissonRatio))<<"\n";

    gsMultiBasis<> dbasis(mp);
    gsInfo<<"Basis (patch 0): "<< mp.patch(0).basis() << "\n";
    mp_def = mp;

    // Boundary conditions
    gsBoundaryConditions<> BCs;
    gsPointLoads<real_t> pLoads = gsPointLoads<real_t>();

    BCs.addCondition(boundary::south, condition_type::dirichlet, 0, 0, false, 0 ); // unknown 2 - z
    BCs.addCondition(boundary::south, condition_type::dirichlet, 0, 0, false, 1 ); // unknown 2 - z

    BCs.addCondition(boundary::north, condition_type::dirichlet, 0, 0, false, 2 ); // unknown 2 - z


    // Symmetry in x-direction:
    BCs.addCondition(boundary::east, condition_type::dirichlet, 0, 0, false, 0 );
    BCs.addCondition(boundary::east, condition_type::clamped, 0, 0, false, 1 );
    BCs.addCondition(boundary::east, condition_type::clamped, 0, 0, false, 2 );

    // Symmetry in y-direction:
    BCs.addCondition(boundary::west, condition_type::clamped, 0, 0, false, 0 );
    BCs.addCondition(boundary::west, condition_type::dirichlet, 0, 0, false, 1 );
    BCs.addCondition(boundary::west, condition_type::clamped, 0, 0, false, 2 );

    // Pressure
    real_t pressure = 1e3;

    std::string dirname = "ArcLengthResults";
    std::string output = dirname + "/Balloon";
    output =  "solution";

    std::string commands = "mkdir -p " + dirname;
    const char *command = commands.c_str();
    system(command);

    // plot geometry
    if (plot)
      gsWriteParaview(mp,dirname + "/" + "mp",1000,true);


    // Linear isotropic material model
    gsFunctionExpr<> force("0","0","0",3);
    gsConstantFunction<> pressFun(pressure,3);
    gsFunctionExpr<> t(std::to_string(thickness),3);
    gsFunctionExpr<> E(std::to_string(E_modulus),3);
    gsFunctionExpr<> nu(std::to_string(PoissonRatio),3);
    gsFunctionExpr<> rho(std::to_string(Density),3);
    gsConstantFunction<> ratio(Ratio,3);

    gsConstantFunction<> alpha1(1.3,3);
    gsConstantFunction<> mu1(6.3e5/4.225e5*mu,3);
    gsConstantFunction<> alpha2(5.0,3);
    gsConstantFunction<> mu2(0.012e5/4.225e5*mu,3);
    gsConstantFunction<> alpha3(-2.0,3);
    gsConstantFunction<> mu3(-0.1e5/4.225e5*mu,3);

    real_t pi = math::atan(1)*4;
    index_t kmax = 1;
    gsVector<> E11(kmax), E22(kmax), G12(kmax), nu12(kmax), nu21(kmax), thick(kmax), phi(kmax);
    E11.setZero(); E22.setZero(); G12.setZero(); nu12.setZero(); nu21.setZero(); thick.setZero(); phi.setZero();
    for (index_t k=0; k != kmax; ++k)
    {
        E11.at(k) = E22.at(k) = E_modulus;
        nu12.at(k) = nu21.at(k) = PoissonRatio;
        G12.at(k) = 0.5 * E_modulus / (1+PoissonRatio);
        thick.at(k) = thickness/kmax;
        phi.at(k) = static_cast<real_t>(k) / kmax * pi/2.0;
    }

    gsConstantFunction<> E11fun(E11,3);
    gsConstantFunction<> E22fun(E22,3);
    gsConstantFunction<> G12fun(G12,3);
    gsConstantFunction<> nu12fun(nu12,3);
    gsConstantFunction<> nu21fun(nu21,3);
    gsConstantFunction<> thickfun(thick,3);
    gsConstantFunction<> phifun(phi,3);

    std::vector<gsFunction<>*> parameters;
    if (material==0) // SvK & Composites
    {
      if (composite)
      {
        parameters.resize(6);
        parameters[0] = &E11fun;
        parameters[1] = &E22fun;
        parameters[2] = &G12fun;
        parameters[3] = &nu12fun;
        parameters[4] = &nu21fun;
        parameters[5] = &phifun;
      }
      else
      {
        parameters.resize(2);
        parameters[0] = &E;
        parameters[1] = &nu;
      }
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
        if (composite)
        {
            options.addInt("Material","Material model: (0): SvK | (1): NH | (2): NH_ext | (3): MR | (4): Ogden",0);
            options.addInt("Implementation","Implementation: (0): Composites | (1): Analytical | (2): Generalized | (3): Spectral",0);
            materialMatrix = getMaterialMatrix<3,real_t>(mp,mp_def,t,parameters,rho,options);
        }
        else
        {
            options.addInt("Material","Material model: (0): SvK | (1): NH | (2): NH_ext | (3): MR | (4): Ogden",0);
            options.addInt("Implementation","Implementation: (0): Composites | (1): Analytical | (2): Generalized | (3): Spectral",1);
            materialMatrix = getMaterialMatrix<3,real_t>(mp,mp_def,t,parameters,rho,options);
        }
    }
    else
    {
        options.addInt("Material","Material model: (0): SvK | (1): NH | (2): NH_ext | (3): MR | (4): Ogden",material);
        options.addSwitch("Compressibility","Compressibility: (false): Imcompressible | (true): Compressible",Compressibility);
        options.addInt("Implementation","Implementation: (0): Composites | (1): Analytical | (2): Generalized | (3): Spectral",impl);
        materialMatrix = getMaterialMatrix<3,real_t>(mp,mp_def,t,parameters,rho,options);
    }

    gsThinShellAssemblerBase<real_t>* assembler;
    assembler = new gsThinShellAssembler<3, real_t, false >(mp,dbasis,BCs,force,materialMatrix);

    // Construct assembler object
    assembler->setPointLoads(pLoads);
    assembler->setPressure(pressFun);

    gsStopwatch stopwatch;
    real_t time = 0.0;

    typedef std::function<gsSparseMatrix<real_t> (gsVector<real_t> const &)>                                Jacobian_t;
    typedef std::function<gsVector<real_t> (gsVector<real_t> const &, real_t, gsVector<real_t> const &) >   ALResidual_t;
    // Function for the Jacobian
    Jacobian_t Jacobian = [&time,&stopwatch,&assembler,&mp_def](gsVector<real_t> const &x)
    {
      stopwatch.restart();
      assembler->constructSolution(x,mp_def);
      assembler->assembleMatrix(mp_def);
      time += stopwatch.stop();

      gsSparseMatrix<real_t> m = assembler->matrix();
      return m;
    };
    // Function for the Residual
    ALResidual_t ALResidual = [&time,&stopwatch,&assembler,&mp_def](gsVector<real_t> const &x, real_t lam, gsVector<real_t> const &force)
    {
      stopwatch.restart();
      assembler->constructSolution(x,mp_def);
      assembler->assembleVector(mp_def);
      gsVector<real_t> Fint = -(assembler->rhs() - force);
      gsVector<real_t> result = Fint - lam * force;
      time += stopwatch.stop();
      return result; // - lam * force;
    };
    // Assemble linear system to obtain the force vector
    assembler->assemble();
    gsVector<> Force = assembler->rhs();

    gsArcLengthIterator<real_t> arcLength(Jacobian, ALResidual, Force);

    arcLength.options().setInt("Solver",1); // CG solver
    arcLength.options().setInt("BifurcationMethod",1); // 0: determinant, 1: eigenvalue
    arcLength.options().setInt("Method",method);
    arcLength.options().setReal("Length",dL);
    arcLength.options().setInt("AngleMethod",0); // 0: step, 1: iteration
    arcLength.options().setSwitch("AdaptiveLength",adaptive);
    arcLength.options().setInt("AdaptiveIterations",5);
    arcLength.options().setReal("Tol",tol);
    arcLength.options().setReal("TolU",tolU);
    arcLength.options().setReal("TolF",tolF);
    arcLength.options().setInt("MaxIter",maxit);
    arcLength.options().setSwitch("Verbose",true);
    arcLength.options().setReal("Relaxation",relax);
    if (quasiNewtonInt>0)
    {
      quasiNewton = true;
      arcLength.options().setInt("QuasiIterations",quasiNewtonInt);
    }
    arcLength.options().setSwitch("Quasi",quasiNewton);


    gsDebug<<arcLength.options();
    arcLength.applyOptions();
    arcLength.initialize();


    gsParaviewCollection collection(dirname + "/" + output);
    gsParaviewCollection Smembrane(dirname + "/" + "membrane");
    gsParaviewCollection Sflexural(dirname + "/" + "flexural");
    gsParaviewCollection Smembrane_p(dirname + "/" + "membrane_p");
    gsMultiPatch<> deformation = mp;

    // Make objects for previous solutions
    real_t Lold = 0;
    gsMatrix<> Uold = Force;
    Uold.setZero();

    gsMatrix<> solVector;
    real_t indicator = 0.0;
    arcLength.setIndicator(indicator); // RESET INDICATOR

    gsMatrix<> lambdas(3,step);
    gsMatrix<> pressures(2,step);
    for (index_t k=0; k<step; k++)
    {
      gsInfo<<"Load step "<< k<<"\n";
      arcLength.step();

      if (!(arcLength.converged()))
        GISMO_ERROR("Loop terminated, arc length method did not converge.\n");

      solVector = arcLength.solutionU();
      Uold = solVector;
      Lold = arcLength.solutionL();

      assembler->constructSolution(solVector,mp_def);

      gsMatrix<> pts(2,1);
      pts<<0.0,1.0;

      lambdas.col(k) = assembler->computePrincipalStretches(pts,mp_def,0);
      pressures.col(k) << pressure*arcLength.solutionL(), pressure*arcLength.solutionL() * assembler->getArea(mp) / assembler->getArea(mp_def);

      deformation = mp_def;
      deformation.patch(0).coefs() -= mp.patch(0).coefs();// assuming 1 patch here

      gsInfo<<"Total ellapsed assembly time: "<<time<<" s\n";

      if (plot)
      {
        gsField<> solField;
        solField= gsField<>(mp,deformation);

        std::string fileName = dirname + "/" + output + util::to_string(k);
        gsWriteParaview<>(solField, fileName, 1000,true);
        fileName = output + util::to_string(k) + "0";
        collection.addTimestep(fileName,k,".vts");
        collection.addTimestep(fileName,k,"_mesh.vtp");
      }
      if (stress)
      {
        std::string fileName;

        gsField<> membraneStress, flexuralStress, membraneStress_p;

        gsPiecewiseFunction<> membraneStresses;
        assembler->constructStress(mp_def,membraneStresses,stress_type::membrane);
        membraneStress = gsField<>(mp,membraneStresses,true);

        fileName = dirname + "/" + "membrane" + util::to_string(k);
        gsWriteParaview( membraneStress, fileName, 1000);
        fileName = "membrane" + util::to_string(k) + "0";
        Smembrane.addTimestep(fileName,k,".vts");

        gsPiecewiseFunction<> flexuralStresses;
        assembler->constructStress(mp_def,flexuralStresses,stress_type::flexural);
        flexuralStress = gsField<>(mp,flexuralStresses, true);

        fileName = dirname + "/" + "flexural" + util::to_string(k);
        gsWriteParaview( flexuralStress, fileName, 1000);
        fileName = "flexural" + util::to_string(k) + "0";
        Sflexural.addTimestep(fileName,k,".vts");

        if (impl==3)
        {
          gsPiecewiseFunction<> membraneStresses_p;
          assembler->constructStress(mp_def,membraneStresses_p,stress_type::principal_stress_membrane);
          membraneStress_p = gsField<>(mp,membraneStresses_p, true);

          fileName = dirname + "/" + "membrane_p" + util::to_string(k);
          gsWriteParaview( membraneStress_p, fileName, 1000);
          fileName = "membrane_p" + util::to_string(k) + "0";
          Smembrane_p.addTimestep(fileName,k,".vts");
        }

      }
    }

    gsInfo<<"Lambdas:\n"<<lambdas<<"\n";
    gsInfo<<"Pressures:\n"<<pressures<<"\n";

    if (plot)
    {
      collection.save();
      Smembrane.save();
      Sflexural.save();
      Smembrane_p.save();
    }

  return result;
}
