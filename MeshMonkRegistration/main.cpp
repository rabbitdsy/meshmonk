#include <iostream>
#include <OpenMesh/Core/IO/MeshIO.hh>
#include <OpenMesh/Core/Mesh/TriMesh_ArrayKernelT.hh>
#include <OpenMesh/Core/IO/reader/OBJReader.hh>
#include <OpenMesh/Core/IO/writer/OBJWriter.hh>
#include <Eigen/Dense>
#include <Eigen/SparseCore>
#include <nanoflann.hpp>
#include <stdio.h>
#include <InlierDetector.hpp>
#include <CorrespondenceFilter.hpp>
#include <RigidTransformer.hpp>
#include <ViscoElasticTransformer.hpp>
#include "global.hpp"
#include <helper_functions.hpp>

using namespace std;



typedef OpenMesh::TriMesh_ArrayKernelT<>  TriMesh;
typedef Eigen::Matrix< int, Eigen::Dynamic, Eigen::Dynamic> MatDynInt; //matrix MxN of type unsigned int
typedef Eigen::Matrix< float, Eigen::Dynamic, Eigen::Dynamic> MatDynFloat; //matrix MxN of type float
typedef Eigen::VectorXf VecDynFloat;
typedef Eigen::Vector3f Vec3Float;
typedef Eigen::Vector4f Vec4Float;
typedef Eigen::Matrix3f Mat3Float;
typedef Eigen::Matrix4f Mat4Float;
typedef Eigen::Matrix< float, Eigen::Dynamic, 3> Vec3Mat; //matrix Mx3 of type float
typedef Eigen::Matrix< float, Eigen::Dynamic, registration::NUM_FEATURES> FeatureMat; //matrix Mx6 of type float
typedef Eigen::Matrix< float, 1, registration::NUM_FEATURES> FeatureVec; //matrix Mx6 of type float
typedef Eigen::SparseMatrix<float, 0, int> SparseMat;
typedef Eigen::Triplet<float> Triplet;
typedef Eigen::SelfAdjointEigenSolver<Mat4Float> EigenVectorDecomposer;


namespace registration {








void fuse_affinities(SparseMat &ioAffinity1,
                    const SparseMat &inAffinity2){
    /*
    # GOAL
    Fuse the two affinity matrices together. The result is inserted into
    ioAffinity1.

    # INPUTS
    -ioAffinity1
    -inAffinity2: dimensions should be transposed of ioAffinity1

    # PARAMETERS

    # OUTPUT
    -ioAffinity1

    # RETURNS
    """
    */
    //# Info and Initialization
    const size_t numRows1 = ioAffinity1.rows();
    const size_t numRows2 = inAffinity2.rows();
    const size_t numCols1 = ioAffinity1.rows();
    const size_t numCols2 = inAffinity2.rows();
    //## Safety check for input sizes
    if((numRows1 != numCols1) || (numCols1 != numRows2)) {
        cerr << "The sizes of the inputted matrices in fuse_affinities are wrong."
        << "Their sizes should be the transpose of each other!" << endl;
    }

    //# Fusing is done by simple averaging
    //## Sum matrices. Note: because Eigen's Sparse matrices require storage
    //## orders to match (row- or column-major) we need to construct a new
    //## temporary matrix of the tranpose of inAffinity2.
    ioAffinity1 += SparseMat(inAffinity2.transpose());
    //## Normalize result
    normalize_sparse_matrix(ioAffinity1);

}


void affinity_to_correspondences(const FeatureMat &inTargetFeatures,
                                    const VecDynFloat &inTargetFlags,
                                    const MatDynFloat &inAffinity,
                                    FeatureMat &outCorrespondingFeatures,
                                    VecDynFloat &outCorrespondingFlags,
                                    const float paramFlagRoundingLimit = 0.9){
    /*
    # GOAL
    This function computes all the corresponding features and flags,
    when the affinity for a set of features and flags is given.

    # INPUTS
    -inTargetFeatures
    -inTargetFlags
    -inAffinity

    # PARAMETERS
    -paramFlagRoundingLimit:
    Flags are binary. Anything over this double is rounded up, whereas anything
    under it is rounded down. A suggested cut-off is 0.9, which means that if
    an element flagged zero contributes 10 percent or to the affinity, the
    corresponding element should be flagged a zero as well.

    # OUTPUT
    -outCorrespondingFeatures
    -outCorrespondingFlags
    */

    //# Info & Initialization
    const size_t numElements = inTargetFeatures.rows();

    //# Simple computation of corresponding features and flags
    outCorrespondingFeatures = inAffinity * inTargetFeatures;
    outCorrespondingFlags = inAffinity * inTargetFlags;

    //# Flag correction.
    //## Flags are binary. We will round them down if lower than the flag
    //## rounding limit (see explanation in parameter description).
    for (size_t i = 0 ; i < numElements ; i++) {
        if (outCorrespondingFlags[i] > paramFlagRoundingLimit){
            outCorrespondingFlags[i] = 1.0;
        }
        else {
            outCorrespondingFlags[i] = 0.0;
        }
    }

}





void rigid_transformation(FeatureMat &ioFeatures,
                            const FeatureMat &inCorrespondingFeatures,
                            const VecDynFloat &inWeights,
                            const bool paramScaling = false) {
    /*
    # GOAL
    This function computes the rigid transformation between a set a features and
    a set of corresponding features. Each correspondence can be weighed between
    0.0 and 1.0.
    The features are automatically transformed, and the function returns the
    transformation matrix that was used.

    # INPUTS
    -ioFeatures
    -inCorrespondingFeatures
    -inWeights

    # PARAMETERS
    -paramScaling:
    Whether or not to allow scaling.

    # OUTPUTS
    -ioFeatures
    */

    //# Info & Initialization
    const size_t numVertices = ioFeatures.rows();
    const size_t numFeatures = ioFeatures.cols();
    MatDynFloat floatingPositions = MatDynFloat::Zero(3, numVertices);
    MatDynFloat correspondingPositions = MatDynFloat::Zero(3, numVertices);

    //## Tranpose the data if necessary
    if ((numVertices > numFeatures) && (numFeatures == NUM_FEATURES)) { //this should normally be the case
        floatingPositions = ioFeatures.leftCols(3).transpose();
        correspondingPositions = inCorrespondingFeatures.leftCols(3).transpose();
    }
    else {
        cerr<< "Warning: input of rigid transformation expects rows to correspond with elements, not features, and to have more elements than features per element." << endl;
    }

    //# Compute the tranformation in 10 steps.
    //## 1. Get the centroids of each set
    Vec3Float floatingCentroid = Vec3Float::Zero();
    Vec3Float correspondingCentroid = Vec3Float::Zero();
    float sumWeights = 0.0;
    //### Weigh and sum all features
    for (size_t i = 0 ; i < numVertices ; i++) {
        floatingCentroid += inWeights[i] * floatingPositions.col(i).segment(0,3);
        correspondingCentroid += inWeights[i] * correspondingPositions.col(i).segment(0,3);
        sumWeights += inWeights[i];
    }
    //### Divide by total weight
    floatingCentroid /= sumWeights;
    correspondingCentroid /= sumWeights;

    //## 2. Compute the Cross Variance matrix
    Mat3Float crossVarianceMatrix = Mat3Float::Zero();
    for(size_t i = 0 ; i < numVertices ; i++) {
        crossVarianceMatrix += inWeights[i] * floatingPositions.col(i) * correspondingPositions.col(i).transpose();
    }
    crossVarianceMatrix = crossVarianceMatrix / sumWeights - floatingCentroid*correspondingCentroid.transpose();

    //## 3. Compute the Anti-Symmetric matrix
    Mat3Float antiSymmetricMatrix = crossVarianceMatrix - crossVarianceMatrix.transpose();

    //## 4. Use the cyclic elements of the Anti-Symmetric matrix to construct delta
    Vec3Float delta = Vec3Float::Zero();
    delta[0] = antiSymmetricMatrix(1,2);
    delta[1] = antiSymmetricMatrix(2,0);
    delta[2] = antiSymmetricMatrix(0,1);

    //## 5. Compute Q
    Mat4Float Q = Mat4Float::Zero();
    Q(0,0) = crossVarianceMatrix.trace();
    Q.block<3,1>(1,0) = delta;
    Q.block<1,3>(0,1) = delta.transpose();
    Q.block<3,3>(1,1) = crossVarianceMatrix + crossVarianceMatrix.transpose()
                        - crossVarianceMatrix.trace() * Mat3Float::Identity();

    //## 6. Now compute the rotation quaternion by finding the eigenvector of Q
    //## of its largest eigenvalue.
    Vec4Float rotQuat = Vec4Float::Zero();
    EigenVectorDecomposer decomposer(Q);
    if (decomposer.info() != Eigen::Success) {
        cerr << "eigenvector decomposer on Q failed!" << std::endl;
        cerr << "Q : " << Q << std::endl;
    }
    size_t indexMaxVal = 0;
    float maxEigenValue = 0.0;
    for ( size_t i = 0 ; i < 4 ; i++ ){
        if ( decomposer.eigenvalues()[i] > maxEigenValue ){
            maxEigenValue = decomposer.eigenvalues()[i];
            indexMaxVal = i;
        }
    }
    rotQuat = decomposer.eigenvectors().col( indexMaxVal );

    //## 7. Construct the rotation matrix
    Mat3Float rotMatTemp = Mat3Float::Zero();
    //### diagonal elements
    rotMatTemp(0,0) = rotQuat[0] * rotQuat[0] + rotQuat[1] * rotQuat[1] - rotQuat[2] * rotQuat[2] - rotQuat[3] * rotQuat[3];
    rotMatTemp(1,1) = rotQuat[0] * rotQuat[0] + rotQuat[2] * rotQuat[2] - rotQuat[1] * rotQuat[1] - rotQuat[3] * rotQuat[3];
    rotMatTemp(2,2) = rotQuat[0] * rotQuat[0] + rotQuat[3] * rotQuat[3] - rotQuat[1] * rotQuat[1] - rotQuat[2] * rotQuat[2];
    //### remaining elements
    rotMatTemp(0,1) = 2.0 * (rotQuat[1] * rotQuat[2] - rotQuat[0] * rotQuat[3]);
    rotMatTemp(1,0) = 2.0 * (rotQuat[1] * rotQuat[2] + rotQuat[0] * rotQuat[3]);
    rotMatTemp(0,2) = 2.0 * (rotQuat[1] * rotQuat[3] + rotQuat[0] * rotQuat[2]);
    rotMatTemp(2,0) = 2.0 * (rotQuat[1] * rotQuat[3] - rotQuat[0] * rotQuat[2]);
    rotMatTemp(1,2) = 2.0 * (rotQuat[2] * rotQuat[3] - rotQuat[0] * rotQuat[1]);
    rotMatTemp(2,1) = 2.0 * (rotQuat[2] * rotQuat[3] + rotQuat[0] * rotQuat[1]);

    //## 8. Estimate scale (if required)
    float scaleFactor = 1.0; //>1 to grow ; <1 to shrink
    if (paramScaling == true){
        float numerator = 0.0;
        float denominator = 0.0;
        for (size_t i = 0 ; i < numVertices ; i++){
            //### Center and rotate the floating position
            Vec3Float newFloatingPos = rotMatTemp * (floatingPositions.block<3,1>(0,i) - floatingCentroid.segment(0, 3));
            //### Center the corresponding position
            Vec3Float newCorrespondingPos = correspondingPositions.block<3,1>(0,i) - correspondingCentroid.segment(0, 3);

            //### Increment numerator and denominator
            numerator += inWeights[i] * newCorrespondingPos.transpose() * newFloatingPos;
            denominator += inWeights[i] * newFloatingPos.transpose() * newFloatingPos;
        }
        scaleFactor = numerator / denominator;
    }


    //## 9. Compute the remaining translation necessary between the centroids
    Vec3Float translation = correspondingCentroid - scaleFactor * rotMatTemp * floatingCentroid;

    //## 10. Compute the entire transformation matrix.
    //### Initialize Matrices
    Mat4Float translationMatrix = Mat4Float::Identity();
    Mat4Float rotationMatrix = Mat4Float::Identity();
    Mat4Float transformationMatrix = Mat4Float::Identity();
    //### Convert to homogeneous transformation matrices
    translationMatrix.block<3, 1>(0,3) = translation;
    rotationMatrix.block<3, 3>(0, 0) = scaleFactor * rotMatTemp;
    //### Matrix transformations on data is performed from right to left.
    //### Translation should be performed before rotating, so translationMatrix
    //### stands right in the multiplication with rotationMatrix.
    transformationMatrix = rotationMatrix * translationMatrix;

    //# Apply the transformation
    //## initialize the position in a [x y z 1] representation
    Vec4Float position4d = Vec4Float::Ones();
    for (size_t i = 0 ; i < numVertices ; i++) {
        //## Extraxt position from feature matrix
        position4d.segment(0, 3) = floatingPositions.block<3,1>(0,i);
        //## Apply transformation and assign to 'position4d' variable again
        position4d = transformationMatrix * position4d;
        ioFeatures.block<1,3>(i,0) = position4d.segment(0, 3);
    }
}





void vector_block_average(const Vec3Mat &inVectors,
                            const VecDynFloat &inWeights,
                            Vec3Float &outVector){
    /*
    GOAL
    This function computes the block average of input vectors (in other words,
    using uniform weights for each contribution).

    INPUT
    -inVectors:
    Values of the vectors of the vector field. The dimensions of this matrix
    are numVectors x 3
    -inWeights:
    The contribution of each vector field vector can be weighed.

    PARAMETERS

    OUTPUT
    -outVector:
    Computed through uniform averaging, taking additional weights (see
    fieldWeights) into account.
    */

    //# Info & Initialization
    size_t numVectors = inVectors.rows();
    size_t dimension = inVectors.cols(); //Assumed to be 3
    if (dimension != 3) {
        cerr << "Dimension is not equal to 3 in vector_block_average() call!" << endl;
    }
    outVector = Vec3Float::Zero();
    float sumWeights = 0.0;

    //# Sum all values
    for (size_t i = 0 ; i < numVectors ; i++) {
        outVector += inVectors.row(i);
        sumWeights += inWeights[i];
    }

    //# Normalize the vector
    outVector /= sumWeights;
}

template <typename VecType, typename VecMatType>
void gaussian_interpolate_scalar_field(const VecType &inQueriedPosition,
                            const VecDynFloat &inScalars,
                            const VecMatType &inVectorPositions,
                            const VecDynFloat &inVectorWeights,
                            float &outQueriedScalar,
                            const float paramSigma){
    /*
    GOAL
    This function computes the gaussian average of a scalar field for a queried
    position. (scalar field = collection of scalars at different vector positions).

    INPUT
    -inQueriedPosition:
    location you want to compute the gaussian average for
    -inScalars:
    Values of the scalars of the vector field.
    -inVectorPositions:
    Locations of the vectors of the vector field.
    -inVectorWeights:
    The contribution of each vector field vector can be weighed.

    PARAMETERS
    -paramSigma:
    The standard deviation of the Gaussian used.

    OUTPUT
    -outQueriedVector:
    Computed through gaussian averaging, taking additional weights (see
    inVectorWeights) into account.
    */

    //# Info & Initialization
    size_t numVectors = inVectorPositions.rows();
    size_t dimension = inVectorPositions.cols(); //Assumed to be 3
    if (dimension != 3) {
        cerr << "Dimension is not equal to 3 in scalar_gaussian_average() call!" << endl;
    }
    outQueriedScalar = 0.0;

    //# Compute Gaussian average;
    float sumWeights = 0.0;
    for (size_t i = 0 ; i < numVectors ; i++) {
        //## Compute distance
        VecType sourceVectorPosition = inVectorPositions.row(i);
        VecType distanceVector = sourceVectorPosition - inQueriedPosition;
        const float distanceSquared = distanceVector.squaredNorm();

        //## From the distance, we can compute the gaussian weight
        const float gaussianWeight = std::exp(-0.5 * distanceSquared / std::pow(paramSigma, 2.0));

        //## Let's take the user-defined weight into account
        const float combinedWeight = gaussianWeight * inVectorWeights[i];

        //## Weigh the current vector field vector and sum it
        outQueriedScalar += combinedWeight * inScalars[i];

        //## Add the weight to the total sum of weights for normalization after.
        sumWeights += combinedWeight;
    }

    //# Normalize the vector
    outQueriedScalar /= sumWeights;

}//end vector_gaussian_average()



template <typename VecType, typename VecMatType>
void gaussian_interpolate_vector_field(const VecType &inQueriedPosition,
                            const VecMatType &inVectors,
                            const VecMatType &inVectorPositions,
                            const VecDynFloat &inVectorWeights,
                            VecType &outQueriedVector,
                            const float paramSigma){
    /*
    GOAL
    This function computes the gaussian average of a vector field for a queried
    position. (vector field = collection of vectors at different vector positions).

    INPUT
    -inQueriedPosition:
    location you want to compute the gaussian average for
    -inVectors:
    Values of the vectors of the vector field. The dimensions of this matrix
    are numVectors x 3
    -inVectorPositions:
    Locations of the vectors of the vector field.
    -inVectorWeights:
    The contribution of each vector field vector can be weighed.

    PARAMETERS
    -paramSigma:
    The standard deviation of the Gaussian used.

    OUTPUT
    -outQueriedVector:
    Computed through gaussian averaging, taking additional weights (see
    inVectorWeights) into account.
    */

    //# Info & Initialization
    size_t numVectors = inVectors.rows();
    size_t dimension = inVectors.cols(); //Assumed to be 3
    if (dimension != 3) {
        cerr << "Dimension is not equal to 3 in vector_gaussian_average() call!" << endl;
    }
    outQueriedVector = VecType::Zero();

    //# Compute Gaussian average;
    float sumWeights = 0.0;
    for (size_t i = 0 ; i < numVectors ; i++) {
        //## Compute distance
        VecType sourceVectorPosition = inVectorPositions.row(i);
        VecType distanceVector = sourceVectorPosition - inQueriedPosition;
        const float distanceSquared = distanceVector.squaredNorm();

        //## From the distance, we can compute the gaussian weight
        const float gaussianWeight = std::exp(-0.5 * distanceSquared / std::pow(paramSigma, 2.0));

        //## Let's take the user-defined weight into account
        const float combinedWeight = gaussianWeight * inVectorWeights[i];

        //## Weigh the current vector field vector and sum it
        outQueriedVector += combinedWeight * inVectors.row(i);

        //## Add the weight to the total sum of weights for normalization after.
        sumWeights += combinedWeight;
    }

    //# Normalize the vector
    outQueriedVector /= sumWeights;

}//end vector_gaussian_average()


template <typename VecType, typename VecMatType>
void gaussian_smoothing_vector_field(const VecMatType &inVectors,
                                    const VecMatType &inVectorPositions,
                                    const VecDynFloat &inVectorWeights,
                                    VecMatType &outSmoothedVectors,
                                    const size_t paramNumNeighbours,
                                    const float paramSigma) {
    /*
    GOAL
    This function performs gaussian smoothing on an entire vector field.

    INPUT
    -inVectorPositions
    -inVectors:
    The vector field that should be smoothed.
    -inVectorWeights

    OUTPUT
    -regulatedFieldVectors:
    The resulting regulated displacement field.


    PARAMETERS
    -paramNumNeighbours:
    For the smoothing, the nearest neighbours for each floating positions have
    to be found. The number should be high enough so that every significant
    contribution (up to a distance of e.g. 3*gaussianSigma) is included. But low
    enough to keep computational speed high.
    -paramSigma:
    The value for sigma of the gaussian used for the smoothing.

    RETURNS
    */

    //# Info & Initialization
    const size_t numVectors = inVectorPositions.rows();
    const size_t numDimensions = inVectorPositions.cols();
    MatDynInt neighbourIndices = MatDynInt::Zero(numVectors, paramNumNeighbours);
    MatDynFloat neighbourSquaredDistances = MatDynFloat::Zero(numVectors, paramNumNeighbours);

    //# Determine for each field node the (closely) neighbouring nodes
    k_nearest_neighbours(inVectorPositions, inVectorPositions, neighbourIndices,
                        neighbourSquaredDistances, paramNumNeighbours, 15);

    //# Use the neighbouring field vectors to smooth each individual field vector
    VecType position = VecType::Zero(numDimensions);
    for (size_t i = 0 ; i < numVectors ; i++) {
        VecType position = inVectorPositions.row(i);

         //## For the current displacement, get all the neighbouring positions,
        //## vectors, and weights (needed for Gaussian smoothing!).
        VecMatType neighbourPositions = VecMatType::Zero(paramNumNeighbours, numDimensions);
        VecMatType neighbourVectors = VecMatType::Zero(paramNumNeighbours, numDimensions);
        VecDynFloat neighbourWeights = VecDynFloat::Zero(paramNumNeighbours);
        for (size_t j = 0 ; j < paramNumNeighbours ; j++) {
            const size_t neighbourIndex = neighbourIndices(i,j);
            neighbourPositions.row(j) = inVectorPositions.row(neighbourIndex);
            neighbourVectors.row(j) = inVectors.row(neighbourIndex);
            neighbourWeights[j] = inVectorWeights[neighbourIndex];
        }

        //## Gaussian averaging of neighbouring displacements
        VecType smoothedVector = VecType::Zero();
        gaussian_interpolate_vector_field(position, neighbourVectors, neighbourPositions,
                                            neighbourWeights, smoothedVector, paramSigma);

        outSmoothedVectors.row(i) = smoothedVector;
    }

}//end gaussian_smoothing_vector_field()

template <typename VecMatType>
void viscoelastic_transformation(VecMatType &ioFloatingPositions,
                                const VecMatType &inCorrespondingPositions,
                                const VecDynFloat &inFloatingWeights,
                                Vec3Mat &ioDisplacementField,
                                const size_t paramNumNeighbourDisplacements,
                                const float paramSigmaSmoothing = 1.0,
                                const size_t paramNumViscousSmoothingIterations = 1,
                                const size_t paramNumElasticSmoothingIterations = 1) {
    /*
    # GOAL
    This function computes the viscoelastic transformation between a set a
    features and a set of corresponding features. Each correspondence can be
    weighed between 0.0 and 1.0.
    The features are automatically transformed, and the function returns the
    displacement field that was used.

    # INPUTS
    -ioFloatingPositions
    -correspondingPositions
    -inFloatingWeights

    # OUTPUTS
    -toBeUpdatedDisplacementField:
    The current displacement field that should be updated.

    # PARAMETERS
    -paramNumNeighbourDisplacements:
    For the regularization, the nearest neighbours for each floating positions
    have to be found. The number should be high enough so that every significant
    contribution (up to a distance of e.g. 3*paramSigmaSmoothing) is included. But low
    enough to keep computational speed high.
    -paramSigmaSmoothing:
    The value for sigma of the gaussian used for the regularization.
    -paramNumViscousSmoothingIterations:
    Number of times the viscous deformation is smoothed.
    -paramNumElasticSmoothingIterations:
    Number of times the elastic deformation is smoothed

    # RETURNS
    */

    //# Info and Initialization
    const size_t numVertices = ioFloatingPositions.rows();

    //# Viscous Part
    //## The 'Force Field' is what drives the deformation: the difference between
    //## the floating vertices and their correspondences. By regulating it,
    //## viscous behaviour is obtained.
    //### Compute the Force Field
    Vec3Mat floatingPositions = ioFloatingPositions.leftCols(3);
    Vec3Mat forceField = inCorrespondingPositions.leftCols(3) - ioFloatingPositions.leftCols(3);


    //### Regulate the Force Field (Gaussian smoothing, iteratively)
    Vec3Mat regulatedForceField = Vec3Mat::Zero(numVertices,3);
    for (size_t i = 0 ; i < paramNumViscousSmoothingIterations ; i++) {
        //### Smooth the force field and save result in 'regulatedForceField'
        gaussian_smoothing_vector_field<Vec3Float, Vec3Mat> (forceField,
                                                            floatingPositions,
                                                            inFloatingWeights,
                                                            regulatedForceField,
                                                            paramNumNeighbourDisplacements,
                                                            paramSigmaSmoothing);

        //### Copy the result into forceField again (in case of more iterations)
        forceField = regulatedForceField;
    }


    //# Elastic Part
    //## Add the regulated Force Field to the current Displacement Field that has
    //## to be updated.
    Vec3Mat unregulatedDisplacementField = ioDisplacementField + regulatedForceField;

    //## Regulate the new Displacement Field (Gaussian smoothing, iteratively)
    for (size_t i = 0 ; i < paramNumElasticSmoothingIterations ; i++) {
        //### Smooth the force field and save result in 'ioDisplacementField'
        gaussian_smoothing_vector_field<Vec3Float, Vec3Mat> (unregulatedDisplacementField, Vec3Mat(ioFloatingPositions.leftCols(3)),
                                        inFloatingWeights, ioDisplacementField,
                                        paramNumNeighbourDisplacements, paramSigmaSmoothing);

        //### Copy the result into unregulatedDisplacementField again (in case
        //### more iterations need to be performed)
        unregulatedDisplacementField = ioDisplacementField;
    }

    //# Apply the transformation to the floating features
    for (size_t i = 0 ; i < numVertices ; i++) {
        ioFloatingPositions.row(i).head(3) += unregulatedDisplacementField.row(i);
    }

}//end viscoelastic_transformation()




} //namespace registration

int main()
{

    /*
    ############################################################################
    ##############################  INPUT  #####################################
    ############################################################################
    */
    //# IO variables
//    const string fuckedUpBunnyDir = "/home/jonatan/kuleuven-algorithms/examples/data/bunny_slightly_rotated.obj";
    const string fuckedUpBunnyDir = "/home/jonatan/projects/kuleuven-algorithms/examples/data/fucked_up_bunny.obj";
    const string bunnyDir = "/home/jonatan/projects/kuleuven-algorithms/examples/data/bunny90.obj";
    const string fuckedUpBunnyResultDir = "/home/jonatan/projects/kuleuven-algorithms/examples/data/fucked_up_bunny_result.obj";

    //# Load meshes and convert to feature matrices
    TriMesh fuckedUpBunny;
    TriMesh bunny;
    FeatureMat floatingFeatures;
    FeatureMat targetFeatures;
    registration::load_obj_to_eigen_features(fuckedUpBunnyDir, fuckedUpBunny, floatingFeatures);
    registration::load_obj_to_eigen_features(bunnyDir, bunny, targetFeatures);
//    floatingFeatures = FeatureMat::Zero(8, NUM_FEATURES);
//    targetFeatures = FeatureMat::Zero(8, NUM_FEATURES);
//    floatingFeatures << 0.1f, 0.1f, 0.1f, 0.0f, 0.0f, 1.0f,
//                        0.1f, 1.1f, 0.1f, 0.0f, 0.0f, 1.0f,
//                        1.1f, 0.1f, 0.1f, 0.0f, 0.0f, 1.0f,
//                        1.1f, 1.1f, 0.1f, 0.0f, 0.0f, 1.0f,
//                        0.1f, 0.1f, 1.1f, 0.0f, 0.0f, 1.0f,
//                        0.1f, 1.1f, 1.1f, 0.0f, 0.0f, 1.0f,
//                        1.1f, 0.1f, 1.1f, 0.0f, 0.0f, 1.0f,
//                        1.1f, 1.1f, 1.1f, 0.0f, 0.0f, 1.0f;
//    targetFeatures << 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
//                      0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
//                      1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
//                      1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
//                      0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
//                      0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
//                      1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
//                      1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f;




     /*
    ############################################################################
    ##############################  TEST SHIZZLE  ##############################
    ############################################################################
    */
    Mat3Float test;
    test << 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0;
    std::cout << test << std::endl;

    float factor = -0.5 / std::pow(1.0, 2.0);
    test *= factor;
    std::cout << test << std::endl;
    //## compute the exponent of the factored squared distance
    test = test.array().exp();
    std::cout << test << std::endl;
    for (size_t i = 0 ; i < 3 ; i++){
        float sum = 0.0f;
        for (size_t j = 0 ; j < 3 ; j++){
              sum += test(i,j);
        }
        test.row(i) /= sum;
    }
    std::cout << test << std::endl;

//    MatDynInt indices;
//    MatDynFloat distancesSquared;
//    radius_nearest_neighbours(floatingFeatures, targetFeatures, indices, distancesSquared, 1.0, 15);
//    cout << "Indices found: \n" << indices << endl;
//    cout << "distances squared found: \n" << distancesSquared << endl;

    /*
    ############################################################################
    ##############################  RIGID ICP  #################################
    ############################################################################
    */

    //# Info & Initialization
    //## Data and matrices
    size_t numFloatingVertices = floatingFeatures.rows();
    size_t numTargetVertices = targetFeatures.rows();
    VecDynFloat floatingWeights = VecDynFloat::Ones(numFloatingVertices);
    VecDynFloat targetFlags = VecDynFloat::Ones(numTargetVertices);
    FeatureMat correspondingFeatures = FeatureMat::Zero(numFloatingVertices, registration::NUM_FEATURES);
    VecDynFloat correspondingFlags = VecDynFloat::Ones(numFloatingVertices);
    //## Parameters
    const size_t numNearestNeighbours = 3;
    const size_t numRigidIterations = 10;
    //## Set up Correspondence Filter
    registration::CorrespondenceFilter correspondenceFilter;
    correspondenceFilter.set_floating_input(&floatingFeatures);
    correspondenceFilter.set_target_input(&targetFeatures, &targetFlags);
    correspondenceFilter.set_output(&correspondingFeatures, &correspondingFlags);
    correspondenceFilter.set_parameters(numNearestNeighbours);
    //## Set up Inlier Detector

    registration::InlierDetector inlierDetector;
    inlierDetector.set_input(&floatingFeatures, &correspondingFeatures,
                                &correspondingFlags);
    inlierDetector.set_output(&floatingWeights);
    inlierDetector.set_parameters(3.0);
    //## Set up the rigid transformer
    registration::RigidTransformer rigidTransformer;
    rigidTransformer.set_input(&correspondingFeatures, &floatingWeights);
    rigidTransformer.set_output(&floatingFeatures);
    rigidTransformer.set_parameters(false);

    //# Loop ICP
    for (size_t iteration = 0 ; iteration < numRigidIterations ; iteration++) {
        //# Determine Correspondences
        //## Compute symmetric wknn correspondences
        //registration::wkkn_correspondences(floatingFeatures, targetFeatures, targetFlags,
        //                    correspondingFeatures, correspondingFlags,
        //                    numNearestNeighbours, true);
        correspondenceFilter.update();


        //# Inlier Detection
        inlierDetector.update();

        //# Compute the transformation
        rigidTransformer.update();
    }

    /*
    ############################################################################
    ##########################  NON-RIGID ICP  #################################
    ############################################################################
    */

    //## Set up viscoelastic transformer
    const size_t numNonrigidIterations = 10;
    size_t smoothingIterations = numNonrigidIterations + 1; //we will use this for the number of smoothing iterations
    registration::ViscoElasticTransformer transformer;
    transformer.set_input(&correspondingFeatures, &floatingWeights);
    transformer.set_output(&floatingFeatures);
    transformer.set_parameters(10, 3.0, smoothingIterations, smoothingIterations);


    for (size_t i = 0 ; i < numNonrigidIterations ; i++) {
        //# Determine Correspondences
        //## Compute symmetric wknn correspondences
        correspondenceFilter.update();


        //# Inlier Detection
        inlierDetector.update();

        //# Visco-Elastic transformation
        transformer.set_parameters(10, 3.0, smoothingIterations,smoothingIterations);
        transformer.update();
        smoothingIterations--;
    }

    /*
    ############################################################################
    ##############################  OUTPUT #####################################
    ############################################################################
    */
    //# Write result to file
    registration::write_eigen_features_to_obj(floatingFeatures, fuckedUpBunny, fuckedUpBunnyResultDir);


    return 0;
}
