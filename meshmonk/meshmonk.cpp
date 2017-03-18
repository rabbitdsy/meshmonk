#include "meshmonk.hpp"

namespace meshmonk{

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus
    //######################################################################################
    //################################  REGISTRATION  ######################################
    //######################################################################################
    /*
    Full Pyramid Nonrigid Registration
    This is the function you'll normally want to call to nonrigidly register a floating mesh to a target mesh.
    */
    void pyramid_registration(FeatureMat& floatingFeatures, const FeatureMat& targetFeatures,
                                const FacesMat& floatingFaces, const FacesMat& targetFaces,
                                const VecDynFloat& floatingFlags, const VecDynFloat& targetFlags,
                                const size_t numIterations/* = 60*/, const size_t numPyramidLayers/* = 3*/,
                                const float downsampleFloatStart/* = 90*/, const float downsampleTargetStart/* = 90*/,
                                const float downsampleFloatEnd/* = 0*/, const float downsampleTargetEnd/* = 0*/,
                                const bool correspondencesSymmetric/* = true*/, const size_t correspondencesNumNeighbours/* = 5*/,
                                const float inlierKappa/* = 4.0f*/,
                                const float transformSigma/* = 3.0f*/,
                                const size_t transformNumViscousIterationsStart/* = 50*/, const size_t transformNumViscousIterationsEnd/* = 1*/,
                                const size_t transformNumElasticIterationsStart/* = 50*/, const size_t transformNumElasticIterationsEnd/* = 1*/)
    {
        registration::PyramidNonrigidRegistration registrator;
        registrator.set_input(floatingFeatures, targetFeatures,
                                floatingFaces, targetFaces,
                                floatingFlags, targetFlags);
        registrator.set_parameters(numIterations, numPyramidLayers,
                                    downsampleFloatStart, downsampleTargetStart,
                                    downsampleFloatEnd, downsampleTargetEnd,
                                    correspondencesSymmetric, correspondencesNumNeighbours,
                                    inlierKappa,
                                    transformSigma,
                                    transformNumViscousIterationsStart, transformNumViscousIterationsEnd,
                                    transformNumElasticIterationsStart, transformNumElasticIterationsEnd);
        registrator.update();
    }

    /*
    Standard Nonrigid Registration
    This is the standard nonrigid registration procedure without pyramid approach, so computationally a bit slower.
    */
    void nonrigid_registration(FeatureMat& floatingFeatures, const FeatureMat& targetFeatures,
                                const FacesMat& floatingFaces, const FacesMat& targetFaces,
                                const VecDynFloat& floatingFlags, const VecDynFloat& targetFlags,
                                const size_t numIterations/* = 60*/,
                                const bool correspondencesSymmetric/* = true*/, const size_t correspondencesNumNeighbours/* = 5*/,
                                const float inlierKappa/* = 4.0f*/,
                                const float transformSigma/* = 3.0f*/,
                                const size_t transformNumViscousIterationsStart/* = 50*/, const size_t transformNumViscousIterationsEnd/* = 1*/,
                                const size_t transformNumElasticIterationsStart/* = 50*/, const size_t transformNumElasticIterationsEnd/* = 1*/)
    {
        registration::NonrigidRegistration registrator;
        registrator.set_input(&floatingFeatures, &targetFeatures,
                                &floatingFaces,
                                &floatingFlags, &targetFlags);
        registrator.set_parameters(correspondencesSymmetric, correspondencesNumNeighbours,
                                    inlierKappa, numIterations,
                                    transformSigma,
                                    transformNumViscousIterationsStart, transformNumViscousIterationsEnd,
                                    transformNumElasticIterationsStart, transformNumElasticIterationsEnd);
        registrator.update();
    }

    /*
    Rigid Registration
    */
    void rigid_registration(FeatureMat& floatingFeatures, const FeatureMat& targetFeatures,
                                const FacesMat& floatingFaces, const FacesMat& targetFaces,
                                const VecDynFloat& floatingFlags, const VecDynFloat& targetFlags,
                                const size_t numIterations/* = 20*/,
                                const bool correspondencesSymmetric/* = true*/, const size_t correspondencesNumNeighbours/* = 5*/,
                                const float inlierKappa/* = 4.0f*/)
    {
        registration::RigidRegistration registrator;
        registrator.set_input(&floatingFeatures, &targetFeatures,
                                &floatingFlags, &targetFlags);
        registrator.set_parameters(correspondencesSymmetric, correspondencesNumNeighbours,
                                    inlierKappa, numIterations);
        registrator.update();
    }




    //######################################################################################
    //############################  REGISTRATION MODULES  ##################################
    //######################################################################################

    //# Correspondences
    void compute_correspondences(const FeatureMat& floatingFeatures, const FeatureMat& targetFeatures,
                                const VecDynFloat& floatingFlags, const VecDynFloat& targetFlags,
                                FeatureMat& correspondingFeatures, VecDynFloat& correspondingFlags,
                                const bool symmetric/* = true*/, const size_t numNeighbours/* = 5*/){
        registration::BaseCorrespondenceFilter* correspondenceFilter = NULL;
        if (symmetric) {
            correspondenceFilter = new registration::SymmetricCorrespondenceFilter();
        }
        else {
            correspondenceFilter = new registration::CorrespondenceFilter();
        }
        correspondenceFilter->set_floating_input(&floatingFeatures, &floatingFlags);
        correspondenceFilter->set_target_input(&targetFeatures, &targetFlags);
        correspondenceFilter->set_output(&correspondingFeatures, &correspondingFlags);
        correspondenceFilter->set_parameters(numNeighbours);
        correspondenceFilter->update();

        delete correspondenceFilter;
    }

    //# Inliers
    void compute_inlier_weights(const FeatureMat& floatingFeatures, const FeatureMat& correspondingFeatures,
                                const VecDynFloat& correspondingFlags, VecDynFloat& inlierWeights,
                                const float kappa/* = 4.0f*/){
        registration::InlierDetector inlierDetector;
        inlierDetector.set_input(&floatingFeatures, &correspondingFeatures,
                                    &correspondingFlags);
        inlierDetector.set_output(&inlierWeights);
        inlierDetector.set_parameters(kappa);
        inlierDetector.update();
    }

    //# Rigid Transformation
    void compute_rigid_transformation(FeatureMat& floatingFeatures, const FeatureMat& correspondingFeatures,
                                    const VecDynFloat& inlierWeights, const bool allowScaling/* = false*/){
        registration::RigidTransformer rigidTransformer;
        rigidTransformer.set_input(&correspondingFeatures, &inlierWeights);
        rigidTransformer.set_output(&floatingFeatures);
        rigidTransformer.set_parameters(allowScaling);
        rigidTransformer.update();
    }

    //# Nonrigid Transformation
    void compute_nonrigid_transformation(FeatureMat& floatingFeatures, const FeatureMat& correspondingFeatures,
                                        const FacesMat& floatingFaces, const VecDynFloat& floatingFlags,
                                        const VecDynFloat& inlierWeights,
                                        const size_t numSmoothingNeighbours/* = 10*/, const float sigmaSmoothing/* = 3.0f*/,
                                        const size_t numViscousIterations/* = 50*/, const size_t numElasticIterations/* = 50*/){
        registration::ViscoElasticTransformer transformer;
        transformer.set_input(&correspondingFeatures, &inlierWeights, &floatingFlags, &floatingFaces);
        transformer.set_output(&floatingFeatures);
        transformer.set_parameters(numSmoothingNeighbours, sigmaSmoothing, numViscousIterations,numElasticIterations);
        transformer.update();
    }


    //# Downsampler
    void downsample_mesh(const FeatureMat& features, const FacesMat& faces,
                        const VecDynFloat& flags,
                        FeatureMat& downsampledFeatures, FacesMat& downsampledFaces,
                        VecDynFloat& downsampledFlags, VecDynInt& originalIndices,
                        const float downsampleRatio/* = 0.8f*/){
        registration::Downsampler downsampler;
        downsampler.set_input(&features, &faces, &flags);
        downsampler.set_output(downsampledFeatures, downsampledFaces,
                                downsampledFlags, originalIndices);
        downsampler.set_parameters(downsampleRatio);
        downsampler.update();
    }


    //# ScaleShifter
    //## The scaleshifter is meant to transition from one scale in the pyramid to the next.
    void scale_shift_mesh(const FeatureMat& previousFeatures, const VecDynInt& previousIndices,
                        FeatureMat& newFeatures, const VecDynInt& newIndices){
        registration::ScaleShifter scaleShifter;
        scaleShifter.set_input(previousFeatures, previousIndices, newIndices);
        scaleShifter.set_output(newFeatures);
        scaleShifter.update();
    }


    //######################################################################################
    //################################  INPUT/OUTPUT  ######################################
    //######################################################################################

    void read_obj_files(const std::string floatingMeshPath, const std::string targetMeshPath,
                        FeatureMat& floatingFeatures, FeatureMat& targetFeatures,
                        FacesMat& floatingFaces, FacesMat& targetFaces){
        registration::import_data(floatingMeshPath, targetMeshPath,
                                    floatingFeatures, targetFeatures,
                                    floatingFaces, targetFaces);
    }

    void write_obj_files(FeatureMat& features, FacesMat& faces, const std::string meshPath){
        registration::export_data(features, faces, meshPath);
    }

#ifdef __cplusplus
}//extern C
#endif // __cplusplus

}//namespace meshmonk
