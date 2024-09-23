/**
* This file is part of ORB-SLAM3
*
* Copyright (C) 2017-2021 Carlos Campos, Richard Elvira, Juan J. Gómez Rodríguez, José M.M. Montiel and Juan D. Tardós, University of Zaragoza.
* Copyright (C) 2014-2016 Raúl Mur-Artal, José M.M. Montiel and Juan D. Tardós, University of Zaragoza.
*
* ORB-SLAM3 is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
* License as published by the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM3 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
* the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with ORB-SLAM3.
* If not, see <http://www.gnu.org/licenses/>.
*/

// 3rdparty
#include <glog/logging.h>
#include <openssl/md5.h>
#include <pangolin/pangolin.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/xml_iarchive.hpp>
#include <boost/archive/xml_oarchive.hpp>
#include <boost/serialization/base_object.hpp>
#include <boost/serialization/string.hpp>
#include <opencv2/imgproc.hpp>
// Local
#include "orbslam3/Atlas.h"
#include "orbslam3/Converter.h"
#include "orbslam3/FrameDrawer.h"
#include "orbslam3/KeyFrameDatabase.h"
#include "orbslam3/LocalMapping.h"
#include "orbslam3/LoopClosing.h"
#include "orbslam3/Map.h"
#include "orbslam3/MapDrawer.h"
#include "orbslam3/MapPoint.h"
#include "orbslam3/Settings.h"
#include "orbslam3/System.h"
#include "orbslam3/Tracking.h"
#include "orbslam3/Viewer.h"

namespace ORB_SLAM3
{

System::System(const std::string &strVocFile, const std::string &strSettingsFile, const eSensor sensor,
               const bool bUseViewer, const int initFr, const std::string &strSequence):
    mSensor(sensor), mpViewer(static_cast<Viewer*>(NULL)), mbReset(false), mbResetActiveMap(false),
    mbActivateLocalizationMode(false), mbDeactivateLocalizationMode(false), mbShutDown(false)
{
    // Output welcome message
    LOG(INFO) << "ORB-SLAM3 Copyright (C) 2017-2020 Carlos Campos, Richard "
                 "Elvira, Juan J. Gómez, José M.M. Montiel and Juan D. Tardós, "
                 "University of Zaragoza";
    LOG(INFO) << "ORB-SLAM2 Copyright (C) 2014-2016 Raúl Mur-Artal, José M.M. "
                 "Montiel and Juan D. Tardós, University of Zaragoza";
    LOG(INFO) << "This program comes with ABSOLUTELY NO WARRANTY. This is free "
                 "software, and you are welcome to redistribute it under "
                 "certain conditions. See LICENSE.txt";

    if(mSensor==MONOCULAR)
        LOG(INFO) << "Input sensor: Monocular";
    else if(mSensor==STEREO)
        LOG(INFO) << "Input sensor: Stereo";
    else if(mSensor==RGBD)
        LOG(INFO) << "Input sensor: RGB-D";
    else if(mSensor==IMU_MONOCULAR)
        LOG(INFO) << "Input sensor: Monocular-Inertial";
    else if(mSensor==IMU_STEREO)
        LOG(INFO) << "Input sensor: Stereo-Inertial";
    else if(mSensor==IMU_RGBD)
        LOG(INFO) << "Input sensor: RGB-D-Inertial";

    //Check settings file
    cv::FileStorage fsSettings(strSettingsFile.c_str(), cv::FileStorage::READ);
    if(!fsSettings.isOpened())
    {
       LOG(ERROR) << "Failed to open settings file at: " << strSettingsFile;
       exit(-1);
    }

    cv::FileNode node = fsSettings["File.version"];
    if(!node.empty() && node.isString() && node.string() == "1.0"){
        settings_ = new Settings(strSettingsFile,mSensor);

        mStrLoadAtlasFromFile = settings_->atlasLoadFile();
        mStrSaveAtlasToFile = settings_->atlasSaveFile();

        LOG(INFO) << *settings_;
    }
    else{
        settings_ = nullptr;
        cv::FileNode node = fsSettings["System.LoadAtlasFromFile"];
        if(!node.empty() && node.isString())
        {
            mStrLoadAtlasFromFile = (string)node;
        }

        node = fsSettings["System.SaveAtlasToFile"];
        if(!node.empty() && node.isString())
        {
            mStrSaveAtlasToFile = (string)node;
        }
    }

    node = fsSettings["loopClosing"];
    bool activeLC = true;
    if(!node.empty())
    {
        activeLC = static_cast<int>(fsSettings["loopClosing"]) != 0;
    }

    mStrVocabularyFilePath = strVocFile;

    bool loadedAtlas = false;

    if(mStrLoadAtlasFromFile.empty())
    {
        //Load ORB Vocabulary
        LOG(INFO) << "Loading ORB Vocabulary. This could take a while";

        mpVocabulary = new ORBVocabulary();
        bool bVocLoad = mpVocabulary->loadFromTextFile(strVocFile);
        if(!bVocLoad)
        {
            LOG(ERROR) << "Wrong path to vocabulary";
            LOG(ERROR) << "Failed to open at: " << strVocFile;
            exit(-1);
        }
        LOG(INFO) << "Vocabulary loaded!";

        //Create KeyFrame Database
        mpKeyFrameDatabase = new KeyFrameDatabase(*mpVocabulary);

        //Create the Atlas
        LOG(INFO) << "Initialization of Atlas from scratch";
        mpAtlas = new Atlas(0);
    }
    else
    {
        //Load ORB Vocabulary
        LOG(INFO) << "Loading ORB Vocabulary. This could take a while";

        mpVocabulary = new ORBVocabulary();
        bool bVocLoad = mpVocabulary->loadFromTextFile(strVocFile);
        if(!bVocLoad)
        {
            LOG(ERROR) << "Wrong path to vocabulary";
            LOG(ERROR) << "Failed to open at: " << strVocFile;
            exit(-1);
        }
        LOG(INFO) << "Vocabulary loaded!";

        //Create KeyFrame Database
        mpKeyFrameDatabase = new KeyFrameDatabase(*mpVocabulary);

        LOG(INFO) << "Load File";

        // Load the file with an earlier session
        //clock_t start = clock();
        LOG(INFO) << "Initialization of Atlas from file: " << mStrLoadAtlasFromFile;
        bool isRead = LoadAtlas(FileType::BINARY_FILE);

        if(!isRead)
        {
            LOG(ERROR) << "Error to load the file, please try with other session file or vocabulary file";
            exit(-1);
        }
        //mpKeyFrameDatabase = new KeyFrameDatabase(*mpVocabulary);

        // LOG(INFO) << "KF in DB: " << mpKeyFrameDatabase->mnNumKFs << "; words: " << mpKeyFrameDatabase->mnNumWords;

        loadedAtlas = true;

        mpAtlas->CreateNewMap();

        //clock_t timeElapsed = clock() - start;
        //unsigned msElapsed = timeElapsed / (CLOCKS_PER_SEC / 1000);
        // LOG(INFO) << "Binary file read in " << msElapsed << " ms";

        //usleep(10*1000*1000);
    }


    if (mSensor==IMU_STEREO || mSensor==IMU_MONOCULAR || mSensor==IMU_RGBD)
        mpAtlas->SetInertialSensor();

    //Create Drawers. These are used by the Viewer
    mpFrameDrawer = new FrameDrawer(mpAtlas);
    mpMapDrawer = new MapDrawer(mpAtlas, strSettingsFile, settings_);

    //Initialize the Tracking thread
    //(it will live in the main thread of execution, the one that called this constructor)
    LOG(INFO) << "Seq. Name: " << strSequence;
    mpTracker = new Tracking(this, mpVocabulary, mpFrameDrawer, mpMapDrawer,
                             mpAtlas, mpKeyFrameDatabase, strSettingsFile, mSensor, settings_, strSequence);

    //Initialize the Local Mapping thread and launch
    mpLocalMapper = new LocalMapping(this, mpAtlas, mSensor==MONOCULAR || mSensor==IMU_MONOCULAR,
                                     mSensor==IMU_MONOCULAR || mSensor==IMU_STEREO || mSensor==IMU_RGBD, strSequence);
    mptLocalMapping = new thread(&LocalMapping::Run,mpLocalMapper);
    mpLocalMapper->mInitFr = initFr;
    if(settings_)
        mpLocalMapper->mThFarPoints = settings_->thFarPoints();
    else
        mpLocalMapper->mThFarPoints = fsSettings["thFarPoints"];
    if(mpLocalMapper->mThFarPoints!=0)
    {
        LOG(INFO) << "Discard points further than " << mpLocalMapper->mThFarPoints << " m from current camera";
        mpLocalMapper->mbFarPoints = true;
    }
    else
        mpLocalMapper->mbFarPoints = false;

    //Initialize the Loop Closing thread and launch
    // mSensor!=MONOCULAR && mSensor!=IMU_MONOCULAR
    mpLoopCloser = new LoopClosing(mpAtlas, mpKeyFrameDatabase, mpVocabulary, mSensor!=MONOCULAR, activeLC); // mSensor!=MONOCULAR);
    mptLoopClosing = new thread(&LoopClosing::Run, mpLoopCloser);

    //Set pointers between threads
    mpTracker->SetLocalMapper(mpLocalMapper);
    mpTracker->SetLoopClosing(mpLoopCloser);

    mpLocalMapper->SetTracker(mpTracker);
    mpLocalMapper->SetLoopCloser(mpLoopCloser);

    mpLoopCloser->SetTracker(mpTracker);
    mpLoopCloser->SetLocalMapper(mpLocalMapper);

    //usleep(10*1000*1000);

    //Initialize the Viewer thread and launch
    if(bUseViewer)
    //if(false) // TODO
    {
        mpViewer = new Viewer(this, mpFrameDrawer,mpMapDrawer,mpTracker,strSettingsFile,settings_);
        mptViewer = new thread(&Viewer::Run, mpViewer);
        mpTracker->SetViewer(mpViewer);
        mpLoopCloser->mpViewer = mpViewer;
        mpViewer->both = mpFrameDrawer->both;
    }
}

Sophus::SE3f System::TrackStereo(const cv::Mat &imLeft, const cv::Mat &imRight, const double &timestamp, const std::vector<IMU::Point>& vImuMeas, std::string filename)
{
    if(mSensor!=STEREO && mSensor!=IMU_STEREO)
    {
        LOG(ERROR) << "TrackStereo called but input sensor was neither Stereo nor Stereo-Inertial";
        exit(-1);
    }

    cv::Mat imLeftToFeed, imRightToFeed;
    if(settings_ && settings_->needToRectify()){
        cv::Mat M1l = settings_->M1l();
        cv::Mat M2l = settings_->M2l();
        cv::Mat M1r = settings_->M1r();
        cv::Mat M2r = settings_->M2r();

        cv::remap(imLeft, imLeftToFeed, M1l, M2l, cv::INTER_LINEAR);
        cv::remap(imRight, imRightToFeed, M1r, M2r, cv::INTER_LINEAR);
    }
    else if(settings_ && settings_->needToResize()){
        cv::resize(imLeft,imLeftToFeed,settings_->newImSize());
        cv::resize(imRight,imRightToFeed,settings_->newImSize());
    }
    else{
        imLeftToFeed = imLeft.clone();
        imRightToFeed = imRight.clone();
    }

    // Check mode change
    {
        std::unique_lock<std::mutex> lock(mMutexMode);
        if(mbActivateLocalizationMode)
        {
            mpLocalMapper->RequestStop();

            // Wait until Local Mapping has effectively stopped
            while(!mpLocalMapper->isStopped())
            {
                usleep(1000);
            }

            mpTracker->InformOnlyTracking(true);
            mbActivateLocalizationMode = false;
        }
        if(mbDeactivateLocalizationMode)
        {
            mpTracker->InformOnlyTracking(false);
            mpLocalMapper->Release();
            mbDeactivateLocalizationMode = false;
        }
    }

    // Check reset
    {
        std::unique_lock<std::mutex> lock(mMutexReset);
        if(mbReset)
        {
            mpTracker->Reset();
            mbReset = false;
            mbResetActiveMap = false;
        }
        else if(mbResetActiveMap)
        {
            mpTracker->ResetActiveMap();
            mbResetActiveMap = false;
        }
    }

    if (mSensor == System::IMU_STEREO)
        for(std::size_t i_imu = 0; i_imu < vImuMeas.size(); i_imu++)
            mpTracker->GrabImuData(vImuMeas[i_imu]);

    LOG(INFO) << "Start GrabImageStereo";
    Sophus::SE3f Tcw = mpTracker->GrabImageStereo(imLeftToFeed,imRightToFeed,timestamp,filename);
    LOG(INFO) << "End GrabImageStereo";

    std::unique_lock<std::mutex> lock2(mMutexState);
    mTrackingState = mpTracker->mState;
    mTrackedMapPoints = mpTracker->mCurrentFrame.mvpMapPoints;
    mTrackedKeyPointsUn = mpTracker->mCurrentFrame.mvKeysUn;

    return Tcw;
}

Sophus::SE3f System::TrackRGBD(const cv::Mat &im, const cv::Mat &depthmap, const double &timestamp, const std::vector<IMU::Point>& vImuMeas, std::string filename)
{
    if(mSensor!=RGBD  && mSensor!=IMU_RGBD)
    {
        LOG(ERROR) << "TrackRGBD called but input sensor was not RGBD";
        exit(-1);
    }

    cv::Mat imToFeed = im.clone();
    cv::Mat imDepthToFeed = depthmap.clone();
    if(settings_ && settings_->needToResize()){
        cv::Mat resizedIm;
        cv::resize(im,resizedIm,settings_->newImSize());
        imToFeed = resizedIm;

        cv::resize(depthmap,imDepthToFeed,settings_->newImSize());
    }

    // Check mode change
    {
        std::unique_lock<std::mutex> lock(mMutexMode);
        if(mbActivateLocalizationMode)
        {
            mpLocalMapper->RequestStop();

            // Wait until Local Mapping has effectively stopped
            while(!mpLocalMapper->isStopped())
            {
                usleep(1000);
            }

            mpTracker->InformOnlyTracking(true);
            mbActivateLocalizationMode = false;
        }
        if(mbDeactivateLocalizationMode)
        {
            mpTracker->InformOnlyTracking(false);
            mpLocalMapper->Release();
            mbDeactivateLocalizationMode = false;
        }
    }

    // Check reset
    {
        std::unique_lock<std::mutex> lock(mMutexReset);
        if(mbReset)
        {
            mpTracker->Reset();
            mbReset = false;
            mbResetActiveMap = false;
        }
        else if(mbResetActiveMap)
        {
            mpTracker->ResetActiveMap();
            mbResetActiveMap = false;
        }
    }

    if (mSensor == System::IMU_RGBD)
        for(std::size_t i_imu = 0; i_imu < vImuMeas.size(); i_imu++)
            mpTracker->GrabImuData(vImuMeas[i_imu]);

    Sophus::SE3f Tcw = mpTracker->GrabImageRGBD(imToFeed,imDepthToFeed,timestamp,filename);

    std::unique_lock<std::mutex> lock2(mMutexState);
    mTrackingState = mpTracker->mState;
    mTrackedMapPoints = mpTracker->mCurrentFrame.mvpMapPoints;
    mTrackedKeyPointsUn = mpTracker->mCurrentFrame.mvKeysUn;
    return Tcw;
}

Sophus::SE3f System::TrackMonocular(const cv::Mat &im, const double &timestamp, const std::vector<IMU::Point>& vImuMeas, std::string filename)
{

    {
        std::unique_lock<std::mutex> lock(mMutexReset);
        if(mbShutDown)
            return Sophus::SE3f();
    }

    if(mSensor!=MONOCULAR && mSensor!=IMU_MONOCULAR)
    {
        LOG(ERROR) << "TrackMonocular called but input sensor was neither Monocular nor Monocular-Inertial";
        exit(-1);
    }

    cv::Mat imToFeed = im.clone();
    if(settings_ && settings_->needToResize()){
        cv::Mat resizedIm;
        cv::resize(im,resizedIm,settings_->newImSize());
        imToFeed = resizedIm;
    }

    // Check mode change
    {
        std::unique_lock<std::mutex> lock(mMutexMode);
        if(mbActivateLocalizationMode)
        {
            mpLocalMapper->RequestStop();

            // Wait until Local Mapping has effectively stopped
            while(!mpLocalMapper->isStopped())
            {
                usleep(1000);
            }

            mpTracker->InformOnlyTracking(true);
            mbActivateLocalizationMode = false;
        }
        if(mbDeactivateLocalizationMode)
        {
            mpTracker->InformOnlyTracking(false);
            mpLocalMapper->Release();
            mbDeactivateLocalizationMode = false;
        }
    }

    // Check reset
    {
        std::unique_lock<std::mutex> lock(mMutexReset);
        if(mbReset)
        {
            mpTracker->Reset();
            mbReset = false;
            mbResetActiveMap = false;
        }
        else if(mbResetActiveMap)
        {
            LOG(WARNING) << "SYSTEM -> Reseting active map in monocular case";
            mpTracker->ResetActiveMap();
            mbResetActiveMap = false;
        }
    }

    if (mSensor == System::IMU_MONOCULAR)
        for(std::size_t i_imu = 0; i_imu < vImuMeas.size(); i_imu++)
            mpTracker->GrabImuData(vImuMeas[i_imu]);

    Sophus::SE3f Tcw = mpTracker->GrabImageMonocular(imToFeed,timestamp,filename);

    std::unique_lock<std::mutex> lock2(mMutexState);
    mTrackingState = mpTracker->mState;
    mTrackedMapPoints = mpTracker->mCurrentFrame.mvpMapPoints;
    mTrackedKeyPointsUn = mpTracker->mCurrentFrame.mvKeysUn;

    return Tcw;
}



void System::ActivateLocalizationMode()
{
    std::unique_lock<std::mutex> lock(mMutexMode);
    mbActivateLocalizationMode = true;
}

void System::DeactivateLocalizationMode()
{
    std::unique_lock<std::mutex> lock(mMutexMode);
    mbDeactivateLocalizationMode = true;
}

bool System::MapChanged()
{
    static int n=0;
    int curn = mpAtlas->GetLastBigChangeIdx();
    if(n<curn)
    {
        n=curn;
        return true;
    }
    else
        return false;
}

void System::Reset()
{
    std::unique_lock<std::mutex> lock(mMutexReset);
    mbReset = true;
}

void System::ResetActiveMap()
{
    std::unique_lock<std::mutex> lock(mMutexReset);
    mbResetActiveMap = true;
}

void System::Shutdown()
{
    {
        std::unique_lock<std::mutex> lock(mMutexReset);
        mbShutDown = true;
    }

    LOG(INFO) << "Shutdown";

    mpLocalMapper->RequestFinish();
    mpLoopCloser->RequestFinish();
    // if (mpViewer) {
    //     mpViewer->RequestFinish();
    //     while (!mpViewer->isFinished()) usleep(5000);
    // }

    // Wait until all thread have effectively stopped
    // while (!mpLocalMapper->isFinished() || !mpLoopCloser->isFinished()
    //        || mpLoopCloser->isRunningGBA()) {
    //     if (!mpLocalMapper->isFinished())
    //         LOG(INFO) << "mpLocalMapper is not finished";
    //     if (!mpLoopCloser->isFinished())
    //         LOG(INFO) << "mpLoopCloser is not finished";
    //     if (mpLoopCloser->isRunningGBA()) {
    //         LOG(INFO) << "mpLoopCloser is running GBA";
    //         LOG(WARNING) << "Break anyway";
    //         break;
    //     }
    //     usleep(5000);
    // }

    if(!mStrSaveAtlasToFile.empty())
    {
        VLOG(1) << "Atlas saving to file " << mStrSaveAtlasToFile;
        SaveAtlas(FileType::BINARY_FILE);
    }

    // if (mpViewer) pangolin::BindToContext("ORB-SLAM2: Map Viewer");

#ifdef REGISTER_TIMES
    mpTracker->PrintTimeStats();
#endif


}

bool System::isShutDown() {
    std::unique_lock<std::mutex> lock(mMutexReset);
    return mbShutDown;
}

void System::SaveTrajectoryTUM(const std::string &filename)
{
    LOG(INFO) << "Saving camera trajectory to " << filename;
    if(mSensor==MONOCULAR)
    {
        LOG(ERROR) << "SaveTrajectoryTUM cannot be used for monocular";
        return;
    }

    std::vector<KeyFrame*> vpKFs = mpAtlas->GetAllKeyFrames();
    std::sort(vpKFs.begin(),vpKFs.end(),KeyFrame::lId);

    // Transform all keyframes so that the first keyframe is at the origin.
    // After a loop closure the first keyframe might not be at the origin.
    Sophus::SE3f Two = vpKFs[0]->GetPoseInverse();

    std::ofstream f;
    f.open(filename.c_str());
    f << std::fixed;

    // Frame pose is stored relative to its reference keyframe (which is optimized by BA and pose graph).
    // We need to get first the keyframe pose and then concatenate the relative transformation.
    // Frames not localized (tracking failure) are not saved.

    // For each frame we have a reference keyframe (lRit), the timestamp (lT) and a flag
    // which is true when tracking failed (lbL).
    auto lRit = mpTracker->mlpReferences.begin();
    auto lT = mpTracker->mlFrameTimes.begin();
    auto lbL = mpTracker->mlbLost.begin();
    for(auto lit=mpTracker->mlRelativeFramePoses.begin(),
        lend=mpTracker->mlRelativeFramePoses.end();lit!=lend;lit++, lRit++, lT++, lbL++)
    {
        if(*lbL)
            continue;

        KeyFrame* pKF = *lRit;

        Sophus::SE3f Trw;

        // If the reference keyframe was culled, traverse the spanning tree to get a suitable keyframe.
        while(pKF->isBad())
        {
            Trw = Trw * pKF->mTcp;
            pKF = pKF->GetParent();
        }

        Trw = Trw * pKF->GetPose() * Two;

        Sophus::SE3f Tcw = (*lit) * Trw;
        Sophus::SE3f Twc = Tcw.inverse();

        Eigen::Vector3f twc = Twc.translation();
        Eigen::Quaternionf q = Twc.unit_quaternion();

        f << std::setprecision(6) << *lT << " " <<  std::setprecision(9) << twc(0) << " " << twc(1) << " " << twc(2) << " " << q.x() << " " << q.y() << " " << q.z() << " " << q.w() << std::endl;
    }
    f.close();

    // LOG(INFO) << "Trajectory saved!";
}

void System::SaveKeyFrameTrajectoryTUM(const std::string &filename)
{
    LOG(INFO) << "Saving keyframe trajectory to " << filename;

    std::vector<KeyFrame*> vpKFs = mpAtlas->GetAllKeyFrames();
    std::sort(vpKFs.begin(),vpKFs.end(),KeyFrame::lId);

    // Transform all keyframes so that the first keyframe is at the origin.
    // After a loop closure the first keyframe might not be at the origin.
    std::ofstream f;
    f.open(filename.c_str());
    f << std::fixed;

    for(std::size_t i=0; i<vpKFs.size(); i++)
    {
        KeyFrame* pKF = vpKFs[i];

       // pKF->SetPose(pKF->GetPose()*Two);

        if(pKF->isBad())
            continue;

        Sophus::SE3f Twc = pKF->GetPoseInverse();
        Eigen::Quaternionf q = Twc.unit_quaternion();
        Eigen::Vector3f t = Twc.translation();
        f << std::setprecision(6) << pKF->mTimeStamp << std::setprecision(7) << " " << t(0) << " " << t(1) << " " << t(2)
          << " " << q.x() << " " << q.y() << " " << q.z() << " " << q.w() << std::endl;

    }

    f.close();
}

void System::SaveTrajectoryEuRoC(const std::string &filename)
{
    LOG(INFO) << "Saving camera trajectory to " << filename;
    // if (mSensor == MONOCULAR) {
    //     LOG(ERROR) << "SaveTrajectoryEuRoC cannot be used for monocular";
    //     return;
    // }

    std::vector<Map*> vpMaps = mpAtlas->GetAllMaps();
    int numMaxKFs = 0;
    Map* pBiggerMap;
    LOG(INFO) << "There are " << vpMaps.size() << " maps in the atlas";
    for(auto pMap :vpMaps)
    {
        LOG(INFO) << "Map " << pMap->GetId() << " has " << pMap->GetAllKeyFrames().size() << " KFs";
        if(pMap->GetAllKeyFrames().size() > numMaxKFs)
        {
            numMaxKFs = pMap->GetAllKeyFrames().size();
            pBiggerMap = pMap;
        }
    }

    std::vector<KeyFrame*> vpKFs = pBiggerMap->GetAllKeyFrames();
    std::sort(vpKFs.begin(),vpKFs.end(),KeyFrame::lId);

    // Transform all keyframes so that the first keyframe is at the origin.
    // After a loop closure the first keyframe might not be at the origin.
    Sophus::SE3f Twb; // Can be word to cam0 or world to b depending on IMU or not.
    if (mSensor==IMU_MONOCULAR || mSensor==IMU_STEREO || mSensor==IMU_RGBD)
        Twb = vpKFs[0]->GetImuPose();
    else
        Twb = vpKFs[0]->GetPoseInverse();

    std::ofstream f;
    f.open(filename.c_str());
    LOG(INFO) << "File open";
    f << std::fixed;

    // Frame pose is stored relative to its reference keyframe (which is optimized by BA and pose graph).
    // We need to get first the keyframe pose and then concatenate the relative transformation.
    // Frames not localized (tracking failure) are not saved.

    // For each frame we have a reference keyframe (lRit), the timestamp (lT) and a flag
    // which is true when tracking failed (lbL).
    auto lRit = mpTracker->mlpReferences.begin();
    auto lT = mpTracker->mlFrameTimes.begin();
    auto lbL = mpTracker->mlbLost.begin();

    // LOG(INFO) << "size mlpReferences: " << mpTracker->mlpReferences.size();
    // LOG(INFO) << "size mlRelativeFramePoses: " << mpTracker->mlRelativeFramePoses.size();
    // LOG(INFO) << "size mpTracker->mlFrameTimes: " << mpTracker->mlFrameTimes.size();
    // LOG(INFO) << "size mpTracker->mlbLost: " << mpTracker->mlbLost.size();


    for(auto lit=mpTracker->mlRelativeFramePoses.begin(),
        lend=mpTracker->mlRelativeFramePoses.end();lit!=lend;lit++, lRit++, lT++, lbL++)
    {
        // LOG(INFO) << "1";
        if(*lbL)
            continue;


        KeyFrame* pKF = *lRit;
        // LOG(INFO) << "KF: " << pKF->mnId;

        Sophus::SE3f Trw;

        // If the reference keyframe was culled, traverse the spanning tree to get a suitable keyframe.
        if (!pKF)
            continue;

        // LOG(INFO) << "2.5";

        while(pKF->isBad())
        {
            // LOG(INFO) << " 2.bad";
            Trw = Trw * pKF->mTcp;
            pKF = pKF->GetParent();
            // LOG(INFO) << "--Parent KF: " << pKF->mnId;
        }

        if(!pKF || pKF->GetMap() != pBiggerMap)
        {
            // LOG(INFO) << "--Parent KF is from another map";
            continue;
        }

        // LOG(INFO) << "3";

        Trw = Trw * pKF->GetPose()*Twb; // Tcp*Tpw*Twb0=Tcb0 where b0 is the new world reference

        // LOG(INFO) << "4";

        if (mSensor == IMU_MONOCULAR || mSensor == IMU_STEREO || mSensor==IMU_RGBD)
        {
            Sophus::SE3f Twb = (pKF->mImuCalib.mTbc * (*lit) * Trw).inverse();
            Eigen::Quaternionf q = Twb.unit_quaternion();
            Eigen::Vector3f twb = Twb.translation();
            f << std::setprecision(6) << 1e9*(*lT) << " " <<  std::setprecision(9) << twb(0) << " " << twb(1) << " " << twb(2) << " " << q.x() << " " << q.y() << " " << q.z() << " " << q.w() << std::endl;
        }
        else
        {
            Sophus::SE3f Twc = ((*lit)*Trw).inverse();
            Eigen::Quaternionf q = Twc.unit_quaternion();
            Eigen::Vector3f twc = Twc.translation();
            f << std::setprecision(6) << 1e9*(*lT) << " " <<  std::setprecision(9) << twc(0) << " " << twc(1) << " " << twc(2) << " " << q.x() << " " << q.y() << " " << q.z() << " " << q.w() << std::endl;
        }

        // LOG(INFO) << "5";
    }
    // LOG(INFO) << "End saving trajectory";
    f.close();
    LOG(INFO) << "End of saving trajectory to " << filename;
}

void System::SaveTrajectoryEuRoC(const std::string &filename, Map* pMap)
{

    LOG(INFO) << "Saving trajectory of map " << pMap->GetId() << " to " << filename;
    // if (mSensor == MONOCULAR) {
    //     LOG(ERROR) << "SaveTrajectoryEuRoC cannot be used for monocular";
    //     return;
    // }

    int numMaxKFs = 0;

    std::vector<KeyFrame*> vpKFs = pMap->GetAllKeyFrames();
    std::sort(vpKFs.begin(),vpKFs.end(),KeyFrame::lId);

    // Transform all keyframes so that the first keyframe is at the origin.
    // After a loop closure the first keyframe might not be at the origin.
    Sophus::SE3f Twb; // Can be word to cam0 or world to b dependingo on IMU or not.
    if (mSensor==IMU_MONOCULAR || mSensor==IMU_STEREO || mSensor==IMU_RGBD)
        Twb = vpKFs[0]->GetImuPose();
    else
        Twb = vpKFs[0]->GetPoseInverse();

    std::ofstream f;
    f.open(filename.c_str());
    // LOG(INFO) << "File open";
    f << std::fixed;

    // Frame pose is stored relative to its reference keyframe (which is optimized by BA and pose graph).
    // We need to get first the keyframe pose and then concatenate the relative transformation.
    // Frames not localized (tracking failure) are not saved.

    // For each frame we have a reference keyframe (lRit), the timestamp (lT) and a flag
    // which is true when tracking failed (lbL).
    auto lRit = mpTracker->mlpReferences.begin();
    auto lT = mpTracker->mlFrameTimes.begin();
    auto lbL = mpTracker->mlbLost.begin();

    // LOG(INFO) << "size mlpReferences: " << mpTracker->mlpReferences.size();
    // LOG(INFO) << "size mlRelativeFramePoses: " << mpTracker->mlRelativeFramePoses.size();
    // LOG(INFO) << "size mpTracker->mlFrameTimes: " << mpTracker->mlFrameTimes.size();
    // LOG(INFO) << "size mpTracker->mlbLost: " << mpTracker->mlbLost.size();


    for(auto lit=mpTracker->mlRelativeFramePoses.begin(),
        lend=mpTracker->mlRelativeFramePoses.end();lit!=lend;lit++, lRit++, lT++, lbL++)
    {
        // LOG(INFO) << "1";
        if(*lbL)
            continue;


        KeyFrame* pKF = *lRit;
        // LOG(INFO) << "KF: " << pKF->mnId;

        Sophus::SE3f Trw;

        // If the reference keyframe was culled, traverse the spanning tree to get a suitable keyframe.
        if (!pKF)
            continue;

        // LOG(INFO) << "2.5";

        while(pKF->isBad())
        {
            // LOG(INFO) << " 2.bad";
            Trw = Trw * pKF->mTcp;
            pKF = pKF->GetParent();
            // LOG(INFO) << "--Parent KF: " << pKF->mnId;
        }

        if(!pKF || pKF->GetMap() != pMap)
        {
            // LOG(INFO) << "--Parent KF is from another map";
            continue;
        }

        // LOG(INFO) << "3";

        Trw = Trw * pKF->GetPose()*Twb; // Tcp*Tpw*Twb0=Tcb0 where b0 is the new world reference

        // LOG(INFO) << "4";

        if (mSensor == IMU_MONOCULAR || mSensor == IMU_STEREO || mSensor==IMU_RGBD)
        {
            Sophus::SE3f Twb = (pKF->mImuCalib.mTbc * (*lit) * Trw).inverse();
            Eigen::Quaternionf q = Twb.unit_quaternion();
            Eigen::Vector3f twb = Twb.translation();
            f << std::setprecision(6) << 1e9*(*lT) << " " <<  std::setprecision(9) << twb(0) << " " << twb(1) << " " << twb(2) << " " << q.x() << " " << q.y() << " " << q.z() << " " << q.w() << std::endl;
        }
        else
        {
            Sophus::SE3f Twc = ((*lit)*Trw).inverse();
            Eigen::Quaternionf q = Twc.unit_quaternion();
            Eigen::Vector3f twc = Twc.translation();
            f << std::setprecision(6) << 1e9*(*lT) << " " <<  std::setprecision(9) << twc(0) << " " << twc(1) << " " << twc(2) << " " << q.x() << " " << q.y() << " " << q.z() << " " << q.w() << std::endl;
        }

        // LOG(INFO) << "5";
    }
    // LOG(INFO) << "End saving trajectory";
    f.close();
    LOG(INFO) << "End of saving trajectory to " << filename;
}

// void System::SaveTrajectoryEuRoC(const string& filename) {
//     LOG(INFO) << "Saving trajectory to " << filename;
//     if (mSensor == MONOCULAR) {
//         LOG(ERROR) << "SaveTrajectoryEuRoC cannot be used for monocular";
//         return;
//     }

//     std::vector<Map*> vpMaps = mpAtlas->GetAllMaps();
//     Map* pBiggerMap;
//     int numMaxKFs = 0;
//     for (auto pMap : vpMaps) {
//         if (pMap->GetAllKeyFrames().size() > numMaxKFs) {
//             numMaxKFs  = pMap->GetAllKeyFrames().size();
//             pBiggerMap = pMap;
//         }
//     }

//     std::vector<KeyFrame*> vpKFs = pBiggerMap->GetAllKeyFrames();
//     std::sort(vpKFs.begin(), vpKFs.end(), KeyFrame::lId);

//     // Transform all keyframes so that the first keyframe is at the origin.
//     // After a loop closure the first keyframe might not be at the origin.
//     Sophus::SE3f
//       Twb; // Can be word to cam0 or world to b dependingo on IMU or not.
//     if (mSensor == IMU_MONOCULAR || mSensor == IMU_STEREO || mSensor == IMU_RGBD)
//         Twb = vpKFs[0]->GetImuPose_();
//     else
//         Twb = vpKFs[0]->GetPoseInverse_();

//     std::ofstream f;
//     f.open(filename.c_str());
//     // LOG(INFO) << "File open";
//     f << std::fixed;

//     // Frame pose is stored relative to its reference keyframe (which is
//     // optimized by BA and pose graph). We need to get first the keyframe pose
//     // and then concatenate the relative transformation. Frames not localized
//     // (tracking failure) are not saved.

//     // For each frame we have a reference keyframe (lRit), the timestamp (lT)
//     // and a flag which is true when tracking failed (lbL).
//     auto lRit = mpTracker->mlpReferences.begin();
//     auto lT = mpTracker->mlFrameTimes.begin();
//     auto lbL  = mpTracker->mlbLost.begin();

//     // LOG(INFO) << "size mlpReferences: " << mpTracker->mlpReferences.size();
//     // LOG(INFO) << "size mlRelativeFramePoses: " << mpTracker->mlRelativeFramePoses.size();
//     // LOG(INFO) << "size mpTracker->mlFrameTimes: " << mpTracker->mlFrameTimes.size();
//     // LOG(INFO) << "size mpTracker->mlbLost: " << mpTracker->mlbLost.size();

//     for (auto lit = mpTracker->mlRelativeFramePoses.begin(),
//          lend = mpTracker->mlRelativeFramePoses.end();
//          lit != lend;
//          lit++, lRit++, lT++, lbL++) {
//         // LOG(INFO) << "1";
//         if (*lbL) continue;

//         KeyFrame* pKF = *lRit;
//         // LOG(INFO) << "KF: " << pKF->mnId;

//         Sophus::SE3f Trw;

//         // If the reference keyframe was culled, traverse the spanning tree to
//         // get a suitable keyframe.
//         if (!pKF) continue;

//         // LOG(INFO) << "2.5";

//         while (pKF->isBad()) {
//             // LOG(INFO) << " 2.bad";
//             Trw = Trw * pKF->mTcp;
//             pKF = pKF->GetParent();
//             // LOG(INFO) << "--Parent KF: " << pKF->mnId;
//         }

//         if (!pKF || pKF->GetMap() != pBiggerMap) {
//             // LOG(INFO) << "--Parent KF is from another map";
//             continue;
//         }

//         // LOG(INFO) << "3";

//         Trw = Trw * pKF->GetPose()
//             * Twb; // Tcp*Tpw*Twb0=Tcb0 where b0 is the new world reference

//         // LOG(INFO) << "4";

//         if (mSensor == IMU_MONOCULAR || mSensor == IMU_STEREO || mSensor == IMU_RGBD) {
//             Sophus::SE3f Tbw = pKF->mImuCalib.Tbc_ * (*lit) * Trw;
//             Sophus::SE3f Twb = Tbw.inverse();

//             Eigen::Vector3f twb  = Twb.translation();
//             Eigen::Quaternionf q = Twb.unit_quaternion();
//             f << std::setprecision(6) << 1e9 * (*lT) << " " << std::setprecision(9)
//               << twb(0) << " " << twb(1) << " " << twb(2) << " " << q.x() << " "
//               << q.y() << " " << q.z() << " " << q.w() << std::endl;
//         } else {
//             Sophus::SE3f Tcw = (*lit) * Trw;
//             Sophus::SE3f Twc = Tcw.inverse();

//             Eigen::Vector3f twc  = Twc.translation();
//             Eigen::Quaternionf q = Twc.unit_quaternion();
//             f << std::setprecision(6) << 1e9 * (*lT) << " " << std::setprecision(9)
//               << twc(0) << " " << twc(1) << " " << twc(2) << " " << q.x() << " "
//               << q.y() << " " << q.z() << " " << q.w() << std::endl;
//         }

//         // LOG(INFO) << "5";
//     }
//     // LOG(INFO) << "End saving trajectory";
//     f.close();
//     LOG(INFO) << "End of saving trajectory to " << filename;
// }

// void System::SaveKeyFrameTrajectoryEuRoC_old(const string& filename) {
//     LOG(INFO) << "Saving keyframe trajectory to " << filename;

//     std::vector<Map*> vpMaps = mpAtlas->GetAllMaps();
//     Map* pBiggerMap;
//     int numMaxKFs = 0;
//     for (auto pMap : vpMaps) {
//         if (pMap->GetAllKeyFrames().size() > numMaxKFs) {
//             numMaxKFs  = pMap->GetAllKeyFrames().size();
//             pBiggerMap = pMap;
//         }
//     }

//     std::vector<KeyFrame*> vpKFs = pBiggerMap->GetAllKeyFrames();
//     std::sort(vpKFs.begin(), vpKFs.end(), KeyFrame::lId);

//     // Transform all keyframes so that the first keyframe is at the origin.
//     // After a loop closure the first keyframe might not be at the origin.
//     std::ofstream f;
//     f.open(filename.c_str());
//     f << std::fixed;

//     for (std::size_t i = 0; i < vpKFs.size(); i++) {
//         KeyFrame* pKF = vpKFs[i];

//         // pKF->SetPose(pKF->GetPose()*Two);

//         if (pKF->isBad()) continue;
//         if (mSensor == IMU_MONOCULAR || mSensor == IMU_STEREO || mSensor == IMU_RGBD) {
//             cv::Mat R       = pKF->GetImuRotation().t();
//             std::vector<float> q = Converter::toQuaternion(R);
//             cv::Mat twb     = pKF->GetImuPosition();
//             f << std::setprecision(6) << 1e9 * pKF->mTimeStamp << " "
//               << std::setprecision(9) << twb.at<float>(0) << " " << twb.at<float>(1)
//               << " " << twb.at<float>(2) << " " << q[0] << " " << q[1] << " "
//               << q[2] << " " << q[3] << std::endl;

//         } else {
//             cv::Mat R       = pKF->GetRotation();
//             std::vector<float> q = Converter::toQuaternion(R);
//             cv::Mat t       = pKF->GetCameraCenter();
//             f << std::setprecision(6) << 1e9 * pKF->mTimeStamp << " "
//               << std::setprecision(9) << t.at<float>(0) << " " << t.at<float>(1)
//               << " " << t.at<float>(2) << " " << q[0] << " " << q[1] << " "
//               << q[2] << " " << q[3] << std::endl;
//         }
//     }
//     f.close();
// }

void System::SaveKeyFrameTrajectoryEuRoC(const std::string &filename)
{
    LOG(INFO) << "Saving keyframe trajectory to " << filename;

    std::vector<Map*> vpMaps = mpAtlas->GetAllMaps();
    Map* pBiggerMap;
    int numMaxKFs = 0;
    for(auto pMap :vpMaps)
    {
        if(pMap && pMap->GetAllKeyFrames().size() > numMaxKFs)
        {
            numMaxKFs = pMap->GetAllKeyFrames().size();
            pBiggerMap = pMap;
        }
    }

    if(!pBiggerMap)
    {
        LOG(WARNING) << "There is not a map!!";
        return;
    }

    std::vector<KeyFrame*> vpKFs = pBiggerMap->GetAllKeyFrames();
    std::sort(vpKFs.begin(),vpKFs.end(),KeyFrame::lId);

    // Transform all keyframes so that the first keyframe is at the origin.
    // After a loop closure the first keyframe might not be at the origin.
    std::ofstream f;
    f.open(filename.c_str());
    f << std::fixed;

    for(std::size_t i=0; i<vpKFs.size(); i++)
    {
        KeyFrame* pKF = vpKFs[i];

       // pKF->SetPose(pKF->GetPose()*Two);

        if(!pKF || pKF->isBad())
            continue;
        if (mSensor == IMU_MONOCULAR || mSensor == IMU_STEREO || mSensor==IMU_RGBD)
        {
            Sophus::SE3f Twb = pKF->GetImuPose();
            Eigen::Quaternionf q = Twb.unit_quaternion();
            Eigen::Vector3f twb = Twb.translation();
            f << std::setprecision(6) << 1e9*pKF->mTimeStamp  << " " <<  std::setprecision(9) << twb(0) << " " << twb(1) << " " << twb(2) << " " << q.x() << " " << q.y() << " " << q.z() << " " << q.w() << std::endl;

        }
        else
        {
            Sophus::SE3f Twc = pKF->GetPoseInverse();
            Eigen::Quaternionf q = Twc.unit_quaternion();
            Eigen::Vector3f t = Twc.translation();
            f << std::setprecision(6) << 1e9*pKF->mTimeStamp << " " <<  std::setprecision(9) << t(0) << " " << t(1) << " " << t(2) << " " << q.x() << " " << q.y() << " " << q.z() << " " << q.w() << std::endl;
        }
    }
    f.close();
}

void System::SaveKeyFrameTrajectoryEuRoC(const std::string &filename, Map* pMap)
{
    LOG(INFO) << "Saving keyframe trajectory of map " << pMap->GetId() << " to " << filename;

    std::vector<KeyFrame*> vpKFs = pMap->GetAllKeyFrames();
    std::sort(vpKFs.begin(),vpKFs.end(),KeyFrame::lId);

    // Transform all keyframes so that the first keyframe is at the origin.
    // After a loop closure the first keyframe might not be at the origin.
    std::ofstream f;
    f.open(filename.c_str());
    f << std::fixed;

    for(std::size_t i=0; i<vpKFs.size(); i++)
    {
        KeyFrame* pKF = vpKFs[i];

        if(!pKF || pKF->isBad())
            continue;
        if (mSensor == IMU_MONOCULAR || mSensor == IMU_STEREO || mSensor==IMU_RGBD)
        {
            Sophus::SE3f Twb = pKF->GetImuPose();
            Eigen::Quaternionf q = Twb.unit_quaternion();
            Eigen::Vector3f twb = Twb.translation();
            f << std::setprecision(6) << 1e9*pKF->mTimeStamp  << " " <<  std::setprecision(9) << twb(0) << " " << twb(1) << " " << twb(2) << " " << q.x() << " " << q.y() << " " << q.z() << " " << q.w() << std::endl;

        }
        else
        {
            Sophus::SE3f Twc = pKF->GetPoseInverse();
            Eigen::Quaternionf q = Twc.unit_quaternion();
            Eigen::Vector3f t = Twc.translation();
            f << std::setprecision(6) << 1e9*pKF->mTimeStamp << " " <<  std::setprecision(9) << t(0) << " " << t(1) << " " << t(2) << " " << q.x() << " " << q.y() << " " << q.z() << " " << q.w() << std::endl;
        }
    }
    f.close();
}

// void System::SaveTrajectoryKITTI(const string& filename) {
//     LOG(INFO) << "Saving camera trajectory to " << filename;
//     if (mSensor == MONOCULAR) {
//         LOG(ERROR) << "SaveTrajectoryKITTI cannot be used for monocular";
//         return;
//     }

//     std::vector<KeyFrame*> vpKFs = mpAtlas->GetAllKeyFrames();
//     std::sort(vpKFs.begin(), vpKFs.end(), KeyFrame::lId);

//     // Transform all keyframes so that the first keyframe is at the origin.
//     // After a loop closure the first keyframe might not be at the origin.
//     cv::Mat Two = vpKFs[0]->GetPoseInverse();

//     std::ofstream f;
//     f.open(filename.c_str());
//     f << std::fixed;

//     // Frame pose is stored relative to its reference keyframe (which is
//     // optimized by BA and pose graph). We need to get first the keyframe pose
//     // and then concatenate the relative transformation. Frames not localized
//     // (tracking failure) are not saved.

//     // For each frame we have a reference keyframe (lRit), the timestamp (lT)
//     // and a flag which is true when tracking failed (lbL).
//     auto lRit = mpTracker->mlpReferences.begin();
//     auto lT = mpTracker->mlFrameTimes.begin();
//     for (auto lit  = mpTracker->mlRelativeFramePoses.begin(),
//               lend = mpTracker->mlRelativeFramePoses.end();
//          lit != lend;
//          lit++, lRit++, lT++) {
//         KeyFrame* pKF = *lRit;

//         cv::Mat Trw = cv::Mat::eye(4, 4, CV_32F);

//         while (pKF->isBad()) {
//             Trw = Trw * Converter::toCvMat(pKF->mTcp.matrix());
//             pKF = pKF->GetParent();
//         }

//         Trw = Trw * pKF->GetPoseCv() * Two;

//         cv::Mat Tcw = (*lit) * Trw;
//         cv::Mat Rwc = Tcw.rowRange(0, 3).colRange(0, 3).t();
//         cv::Mat twc = -Rwc * Tcw.rowRange(0, 3).col(3);

//         f << std::setprecision(9) << Rwc.at<float>(0, 0) << " "
//           << Rwc.at<float>(0, 1) << " " << Rwc.at<float>(0, 2) << " "
//           << twc.at<float>(0) << " " << Rwc.at<float>(1, 0) << " "
//           << Rwc.at<float>(1, 1) << " " << Rwc.at<float>(1, 2) << " "
//           << twc.at<float>(1) << " " << Rwc.at<float>(2, 0) << " "
//           << Rwc.at<float>(2, 1) << " " << Rwc.at<float>(2, 2) << " "
//           << twc.at<float>(2) << std::endl;
//     }
//     f.close();
// }

void System::SaveTrajectoryKITTI(const std::string &filename)
{
    LOG(INFO) << "Saving camera trajectory to " << filename;
    if(mSensor==MONOCULAR)
    {
        LOG(ERROR) << "SaveTrajectoryKITTI cannot be used for monocular";
        return;
    }

    std::vector<KeyFrame*> vpKFs = mpAtlas->GetAllKeyFrames();
    std::sort(vpKFs.begin(),vpKFs.end(),KeyFrame::lId);

    // Transform all keyframes so that the first keyframe is at the origin.
    // After a loop closure the first keyframe might not be at the origin.
    Sophus::SE3f Tow = vpKFs[0]->GetPoseInverse();

    std::ofstream f;
    f.open(filename.c_str());
    f << std::fixed;

    // Frame pose is stored relative to its reference keyframe (which is optimized by BA and pose graph).
    // We need to get first the keyframe pose and then concatenate the relative transformation.
    // Frames not localized (tracking failure) are not saved.

    // For each frame we have a reference keyframe (lRit), the timestamp (lT) and a flag
    // which is true when tracking failed (lbL).
    auto lRit = mpTracker->mlpReferences.begin();
    auto lT = mpTracker->mlFrameTimes.begin();
    for(auto lit=mpTracker->mlRelativeFramePoses.begin(),
        lend=mpTracker->mlRelativeFramePoses.end();lit!=lend;lit++, lRit++, lT++)
    {
        KeyFrame* pKF = *lRit;

        Sophus::SE3f Trw;

        if(!pKF)
            continue;

        while(pKF->isBad())
        {
            Trw = Trw * pKF->mTcp;
            pKF = pKF->GetParent();
        }

        Trw = Trw * pKF->GetPose() * Tow;

        Sophus::SE3f Tcw = (*lit) * Trw;
        Sophus::SE3f Twc = Tcw.inverse();
        Eigen::Matrix3f Rwc = Twc.rotationMatrix();
        Eigen::Vector3f twc = Twc.translation();

        f << std::setprecision(9) << Rwc(0,0) << " " << Rwc(0,1)  << " " << Rwc(0,2) << " "  << twc(0) << " " <<
             Rwc(1,0) << " " << Rwc(1,1)  << " " << Rwc(1,2) << " "  << twc(1) << " " <<
             Rwc(2,0) << " " << Rwc(2,1)  << " " << Rwc(2,2) << " "  << twc(2) << std::endl;
    }
    f.close();
}


void System::SaveDebugData(const int &initIdx)
{
    // 0. Save initialization trajectory
    SaveTrajectoryEuRoC("init_FrameTrajectoy_" +to_string(mpLocalMapper->mInitSect)+ "_" + std::to_string(initIdx)+".txt");

    // 1. Save scale
    std::ofstream f;
    f.open("init_Scale_" + std::to_string(mpLocalMapper->mInitSect) + ".txt", std::ios_base::app);
    f << std::fixed;
    f << mpLocalMapper->mScale << std::endl;
    f.close();

    // 2. Save gravity direction
    f.open("init_GDir_" +to_string(mpLocalMapper->mInitSect)+ ".txt", std::ios_base::app);
    f << std::fixed;
    f << mpLocalMapper->mRwg(0,0) << "," << mpLocalMapper->mRwg(0,1) << "," << mpLocalMapper->mRwg(0,2) << std::endl;
    f << mpLocalMapper->mRwg(1,0) << "," << mpLocalMapper->mRwg(1,1) << "," << mpLocalMapper->mRwg(1,2) << std::endl;
    f << mpLocalMapper->mRwg(2,0) << "," << mpLocalMapper->mRwg(2,1) << "," << mpLocalMapper->mRwg(2,2) << std::endl;
    f.close();

    // 3. Save computational cost
    f.open("init_CompCost_" +to_string(mpLocalMapper->mInitSect)+ ".txt", std::ios_base::app);
    f << std::fixed;
    f << mpLocalMapper->mCostTime << std::endl;
    f.close();

    // 4. Save biases
    f.open("init_Biases_" +to_string(mpLocalMapper->mInitSect)+ ".txt", std::ios_base::app);
    f << std::fixed;
    f << mpLocalMapper->mbg(0) << "," << mpLocalMapper->mbg(1) << "," << mpLocalMapper->mbg(2) << std::endl;
    f << mpLocalMapper->mba(0) << "," << mpLocalMapper->mba(1) << "," << mpLocalMapper->mba(2) << std::endl;
    f.close();

    // 5. Save covariance matrix
    f.open("init_CovMatrix_" +to_string(mpLocalMapper->mInitSect)+ "_" +to_string(initIdx)+".txt", std::ios_base::app);
    f << std::fixed;
    for(int i=0; i<mpLocalMapper->mcovInertial.rows(); i++)
    {
        for(int j=0; j<mpLocalMapper->mcovInertial.cols(); j++)
        {
            if(j!=0)
                f << ",";
            f << std::setprecision(15) << mpLocalMapper->mcovInertial(i,j);
        }
        f << std::endl;
    }
    f.close();

    // 6. Save initialization time
    f.open("init_Time_" +to_string(mpLocalMapper->mInitSect)+ ".txt", std::ios_base::app);
    f << std::fixed;
    f << mpLocalMapper->mInitTime << std::endl;
    f.close();
}


int System::GetTrackingState()
{
    std::unique_lock<std::mutex> lock(mMutexState);
    return mTrackingState;
}

std::vector<MapPoint*> System::GetTrackedMapPoints()
{
    std::unique_lock<std::mutex> lock(mMutexState);
    return mTrackedMapPoints;
}

std::vector<cv::KeyPoint> System::GetTrackedKeyPointsUn()
{
    std::unique_lock<std::mutex> lock(mMutexState);
    return mTrackedKeyPointsUn;
}

double System::GetTimeFromIMUInit()
{
    double aux = mpLocalMapper->GetCurrKFTime()-mpLocalMapper->mFirstTs;
    if ((aux>0.) && mpAtlas->isImuInitialized())
        return mpLocalMapper->GetCurrKFTime()-mpLocalMapper->mFirstTs;
    else
        return 0.f;
}

bool System::isLost()
{
    if (!mpAtlas->isImuInitialized())
        return false;
    else
    {
        if ((mpTracker->mState==Tracking::LOST)) //||(mpTracker->mState==Tracking::RECENTLY_LOST))
            return true;
        else
            return false;
    }
}


bool System::isFinished()
{
    return (GetTimeFromIMUInit()>0.1);
}

void System::ChangeDataset()
{
    if(mpAtlas->GetCurrentMap()->KeyFramesInMap() < 12)
    {
        mpTracker->ResetActiveMap();
    }
    else
    {
        mpTracker->CreateMapInAtlas();
    }

    mpTracker->NewDataset();
}

float System::GetImageScale()
{
    return mpTracker->GetImageScale();
}

#ifdef REGISTER_TIMES
void System::InsertRectTime(double& time)
{
    mpTracker->vdRectStereo_ms.push_back(time);
}

void System::InsertResizeTime(double& time)
{
    mpTracker->vdResizeImage_ms.push_back(time);
}

void System::InsertTrackTime(double& time)
{
    mpTracker->vdTrackTotal_ms.push_back(time);
}
#endif

void System::SaveAtlas(int type){
    if(!mStrSaveAtlasToFile.empty())
    {
        //clock_t start = clock();

        // Save the current session
        mpAtlas->PreSave();

        std::string pathSaveFileName = "./";
        pathSaveFileName = pathSaveFileName.append(mStrSaveAtlasToFile);
        pathSaveFileName = pathSaveFileName.append(".osa");

        std::string strVocabularyChecksum = CalculateCheckSum(mStrVocabularyFilePath,TEXT_FILE);
        std::size_t found = mStrVocabularyFilePath.find_last_of("/\\");
        std::string strVocabularyName = mStrVocabularyFilePath.substr(found+1);

        if(type == TEXT_FILE) // File text
        {
            LOG(INFO) << "Starting to write the save text file";
            std::remove(pathSaveFileName.c_str());
            std::ofstream ofs(pathSaveFileName, std::ios::binary);
            boost::archive::text_oarchive oa(ofs);

            oa << strVocabularyName;
            oa << strVocabularyChecksum;
            oa << mpAtlas;
            LOG(INFO) << "End to write the save text file";
        }
        else if(type == BINARY_FILE) // File binary
        {
            LOG(INFO) << "Starting to write the save binary file";
            std::remove(pathSaveFileName.c_str());
            std::ofstream ofs(pathSaveFileName, std::ios::binary);
            boost::archive::binary_oarchive oa(ofs);
            oa << strVocabularyName;
            oa << strVocabularyChecksum;
            oa << mpAtlas;
            LOG(INFO) << "End to write save binary file";
        }
    }
}

bool System::LoadAtlas(int type)
{
    std::string strFileVoc, strVocChecksum;
    bool isRead = false;

    std::string pathLoadFileName = "./";
    pathLoadFileName = pathLoadFileName.append(mStrLoadAtlasFromFile);
    pathLoadFileName = pathLoadFileName.append(".osa");

    if(type == TEXT_FILE) // File text
    {
        LOG(INFO) << "Starting to read the save text file ";
        std::ifstream ifs(pathLoadFileName, std::ios::binary);
        if(!ifs.good())
        {
            LOG(ERROR) << "Load file not found";
            return false;
        }
        boost::archive::text_iarchive ia(ifs);
        ia >> strFileVoc;
        ia >> strVocChecksum;
        ia >> mpAtlas;
        LOG(INFO) << "End to load the save text file ";
        isRead = true;
    }
    else if(type == BINARY_FILE) // File binary
    {
        LOG(INFO) << "Starting to read the save binary file";
        std::ifstream ifs(pathLoadFileName, std::ios::binary);
        if(!ifs.good())
        {
            LOG(ERROR) << "Load file not found";
            return false;
        }
        boost::archive::binary_iarchive ia(ifs);
        ia >> strFileVoc;
        ia >> strVocChecksum;
        ia >> mpAtlas;
        LOG(INFO) << "End to load the save binary file";
        isRead = true;
    }

    if(isRead)
    {
        //Check if the vocabulary is the same
        std::string strInputVocabularyChecksum = CalculateCheckSum(mStrVocabularyFilePath,TEXT_FILE);

        if(strInputVocabularyChecksum.compare(strVocChecksum) != 0)
        {
            LOG(ERROR) << "The vocabulary load isn't the same which the load session was created ";
            LOG(ERROR) << "Vocabulary name: " << strFileVoc;
            return false; // Both are differents
        }

        mpAtlas->SetKeyFrameDababase(mpKeyFrameDatabase);
        mpAtlas->SetORBVocabulary(mpVocabulary);
        mpAtlas->PostLoad();

        return true;
    }
    return false;
}

std::string System::CalculateCheckSum(std::string filename, int type)
{
    std::string checksum = "";

    unsigned char c[MD5_DIGEST_LENGTH];

    std::ios_base::openmode flags = std::ios::in;
    if(type == BINARY_FILE) // Binary file
        flags = std::ios::in | std::ios::binary;

    std::ifstream f(filename.c_str(), flags);
    if ( !f.is_open() )
    {
        LOG(ERROR) << "Unable to open the in file " << filename << " for Md5 hash";
        return checksum;
    }

    MD5_CTX md5Context;
    char buffer[1024];

    MD5_Init (&md5Context);
    while ( int count = f.readsome(buffer, sizeof(buffer)))
    {
        MD5_Update(&md5Context, buffer, count);
    }

    f.close();

    MD5_Final(c, &md5Context );

    for(int i = 0; i < MD5_DIGEST_LENGTH; i++)
    {
        char aux[10];
        sprintf(aux,"%02x", c[i]);
        checksum = checksum + aux;
    }

    return checksum;
}

} //namespace ORB_SLAM
