/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2018                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <modules/dsn/rendering/renderablesignals.h>

#include <modules/base/basemodule.h>
#include <openspace/documentation/documentation.h>
#include <openspace/documentation/verifier.h>
#include <openspace/engine/globals.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/scene/translation.h>
#include <openspace/util/updatestructures.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/opengl/programobject.h>
#include <openspace/util/spicemanager.h>
#include <openspace/interaction/navigationhandler.h>

namespace {
    constexpr const char* ProgramName = "SignalsProgram";
    constexpr const char* _loggerCat = "RenderableSignals";
    constexpr const char* KeyStationSites = "StationSites";

    constexpr const std::array<const char*, 3> UniformNames = {
        "modelViewStation","modelViewSpacecraft", "projectionTransform"
    };

    constexpr openspace::properties::Property::PropertyInfo SiteColorsInfo = {
        "SiteColors",
        "SiteColors",
        "This value determines the RGB main color for the communication "
        "signals to and from different sites on Earth."
    };

    constexpr openspace::properties::Property::PropertyInfo LineWidthInfo = {
        "LineWidth",
        "Line Width",
        "This value specifies the line width of the signals. "
    };

} // namespace

namespace openspace {

documentation::Documentation RenderableSignals::Documentation() {
    using namespace documentation;
    return {
        "Renderable Signals",
        "dsn_renderable_renderablesignals",
        {
            {
                SiteColorsInfo.identifier,
                new TableVerifier,
                Optional::No,
                SiteColorsInfo.description
            },
            {
                KeyStationSites,
                new TableVerifier,
                Optional::No,
                "This is a map of the individual stations to their "
                "respective site location on Earth."
            },
            {
                LineWidthInfo.identifier,
                new DoubleVerifier,
                Optional::Yes,
                LineWidthInfo.description
            }
        }
    };
}

RenderableSignals::RenderableSignals(const ghoul::Dictionary& dictionary)
    : Renderable(dictionary)
    , _lineWidth(LineWidthInfo, 2.f, 1.f, 20.f)
{
    if (dictionary.hasKeyAndValue<ghoul::Dictionary>(SiteColorsInfo.identifier)) {
        ghoul::Dictionary siteColorDictionary = dictionary.value<ghoul::Dictionary>(SiteColorsInfo.identifier);
        std::vector<std::string> siteNames = siteColorDictionary.keys();

        for (int siteIndex = 0; siteIndex < siteNames.size(); siteIndex++)
        {
            const char* siteColorIdentifier = siteNames.at(siteIndex).c_str();
            openspace::properties::Property::PropertyInfo SiteColorsInfo = {
                siteColorIdentifier,
                siteColorIdentifier,
                "This value determines the RGB main color for signals "
                "of communication to and from different sites on Earth."
            };
            std::string site = siteNames[siteIndex];
            glm::vec3 siteColor = siteColorDictionary.value<glm::vec3>(siteNames.at(siteIndex));
            _siteColors.push_back(std::make_unique<properties::Vec3Property>(SiteColorsInfo, siteColor, glm::vec3(0.f), glm::vec3(1.f)));
            _siteToIndex[siteNames.at(siteIndex)] = siteIndex;
            addProperty(_siteColors.back().get());
        }
    }

    if (dictionary.hasKeyAndValue<ghoul::Dictionary>(KeyStationSites)) {
        ghoul::Dictionary stationDictionary = dictionary.value<ghoul::Dictionary>(KeyStationSites);
        std::vector<std::string> keys = stationDictionary.keys();

        for (int i = 0; i < keys.size(); i++)
        {   
            std::string station = keys.at(i);
            std::string site = stationDictionary.value<std::string>(keys.at(i));
            _stationToSite[station] = site;
        }
    }
 
    if (dictionary.hasKeyAndValue<double>(LineWidthInfo.identifier)) {
        _lineWidth = static_cast<float>(dictionary.value<double>(
            LineWidthInfo.identifier
        ));
    }
    addProperty(_lineWidth);

    std::unique_ptr<ghoul::Dictionary> dictionaryPtr = std::make_unique<ghoul::Dictionary>(dictionary);
    extractData(dictionaryPtr);
}

void RenderableSignals::initializeGL() {
    _programObject = BaseModule::ProgramObjectManager.request(
        ProgramName,
        []() -> std::unique_ptr<ghoul::opengl::ProgramObject> {
            return global::renderEngine.buildRenderProgram(
                ProgramName,
                absPath("${MODULE_DSN}/shaders/renderablesignals_vs.glsl"),
                absPath("${MODULE_DSN}/shaders/renderablesignals_fs.glsl")
            );
        }
    );

    ghoul::opengl::updateUniformLocations(*_programObject, _uniformCache, UniformNames);

    setRenderBin(Renderable::RenderBin::Overlay);

    // We don't need an index buffer, so we keep it at the default value of 0
    glGenVertexArrays(1, &_lineRenderInformation._vaoID);
    glGenBuffers(1, &_lineRenderInformation._vBufferID);
}

void RenderableSignals::deinitializeGL() {

    glDeleteVertexArrays(1, &_lineRenderInformation._vaoID);
    glDeleteBuffers(1, &_lineRenderInformation._vBufferID);

    BaseModule::ProgramObjectManager.release(
        ProgramName,
        [](ghoul::opengl::ProgramObject* p) {
            global::renderEngine.removeRenderProgram(p);
        }
    );
    _programObject = nullptr;
}

bool RenderableSignals::isReady() const {
    return _programObject != nullptr;
}

// Unbind buffers and arrays
inline void unbindGL() {
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void RenderableSignals::render(const RenderData& data, RendererTasks&) {
    _programObject->activate();

    //The stations are statically translated with respect to Earth
    glm::dmat4 modelTransformStation = global::renderEngine.scene()->sceneGraphNode("Earth")->modelTransform();

    _programObject->setUniform(_uniformCache.modelViewStation,
        data.camera.combinedViewMatrix() * modelTransformStation);

    _programObject->setUniform(_uniformCache.modelViewSpacecraft,
        data.camera.combinedViewMatrix()  * _lineRenderInformation._localTransformSpacecraft);

    _programObject->setUniform(_uniformCache.projection, data.camera.projectionMatrix());

    const bool usingFramebufferRenderer =
        global::renderEngine.rendererImplementation() ==
        RenderEngine::RendererImplementation::Framebuffer;

    if (usingFramebufferRenderer) {
        glDepthMask(false);
        //glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    }
    glLineWidth(_lineWidth);

    glBindVertexArray(_lineRenderInformation._vaoID);

    glDrawArrays(
        GL_LINES,
        _lineRenderInformation.first,
        _lineRenderInformation.count
    );

    //unbind vertex array and buffers
    unbindGL();

    if (usingFramebufferRenderer) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(true);
    }

    _programObject->deactivate();
}

void RenderableSignals::update(const UpdateData& data) {

    double currentTime = data.time.j2000Seconds();
    //Todo: change this magic number. 86400 equals 24hrs in seconds
    double endTime = 86400;

    //Bool if the current time is within the timeframe for the currently loaded data
    const bool isTimeInFileInterval = (currentTime >= SignalManager::_signalData.sequenceStartTime) &&
        (currentTime < SignalManager::_signalData.sequenceStartTime + endTime);

    //Reload data if it is not relevant anymore
    if (!isTimeInFileInterval) {
        SignalManager::_signalData.isLoaded = false;

        int activeFileIndex = findFileIndexForCurrentTime(currentTime, SignalManager::_fileStartTimes);
        //parse data for that file
        if (!SignalManager::_signalData.isLoaded)
        {
            SignalManager::signalParser(activeFileIndex);

        }
        else
            return;
    }

    // Make space for the vertex renderinformation
    _vertexArray.clear();

    //update focusnode information used to calculate spacecraft positions
    _focusNode = global::navigationHandler.focusNode();
    _lineRenderInformation._localTransformSpacecraft = glm::translate(glm::dmat4(1.0), _focusNode->worldPosition());

    //Todo; keep track of active index for signalvector, or swap for loop for binary search
    for (int i = 0; i < SignalManager::_signalData.signals.size(); i++) {

        SignalManager::Signal currentSignal = SignalManager::_signalData.signals[i];
        if (isSignalActive(currentTime, currentSignal.startTime, currentSignal.endTime, currentSignal.lightTravelTime)) {
            currentSignal.timeSinceStart = currentTime - Time::convertTime(currentSignal.startTime);
            pushSignalDataToVertexArray(currentSignal);
        }
    };

 
    // ... and upload them to the GPU
    glBindVertexArray(_lineRenderInformation._vaoID);
    glBindBuffer(GL_ARRAY_BUFFER, _lineRenderInformation._vBufferID);
    glBufferData(
        GL_ARRAY_BUFFER,
        _vertexArray.size() * sizeof(float),
        _vertexArray.data(),
        GL_STATIC_DRAW
    );

    // position attributes
    glVertexAttribPointer(_vaLocVer, _sizeThreeVal, GL_FLOAT, GL_FALSE, sizeof(ColorVBOLayout) + sizeof(PositionVBOLayout) + sizeof(DistanceVBOLayout) + sizeof(float), (void*)0);
    glEnableVertexAttribArray(_vaLocVer);
    // color attributes
    glVertexAttribPointer(_vaLocCol, _sizeFourVal, GL_FLOAT, GL_FALSE, sizeof(ColorVBOLayout) + sizeof(PositionVBOLayout) + sizeof(DistanceVBOLayout) + sizeof(float), (void*)(sizeof(PositionVBOLayout)));
    glEnableVertexAttribArray(_vaLocCol);
    // distance attributes
    glVertexAttribPointer(_vaLocDist, _sizeOneVal, GL_FLOAT, GL_FALSE, sizeof(ColorVBOLayout) + sizeof(PositionVBOLayout) + sizeof(DistanceVBOLayout) + sizeof(float), (void*)(sizeof(PositionVBOLayout) + sizeof(ColorVBOLayout)));
    glEnableVertexAttribArray(_vaLocDist);
    // time attribute
    glVertexAttribPointer(_vaLocTimeSinceStart, _sizeOneVal, GL_FLOAT, GL_FALSE, sizeof(ColorVBOLayout) + sizeof(PositionVBOLayout) + sizeof(DistanceVBOLayout) + sizeof(float), (void*)(sizeof(PositionVBOLayout) + sizeof(ColorVBOLayout)+ sizeof(float)));
    glEnableVertexAttribArray(_vaLocTimeSinceStart);

    // Directly render the entire step
    _lineRenderInformation.first = 0;
    _lineRenderInformation.count = static_cast<GLsizei>(_vertexArray.size() / (_sizeThreeVal + _sizeFourVal + 2 *_sizeOneVal));

    //unbind vertexArray
    unbindGL();
}

int RenderableSignals::findFileIndexForCurrentTime(double time, std::vector<double> vec) {
    // upper_bound has O(log n) for sorted vectors, more efficient than for loop
    auto iter = std::upper_bound(vec.begin(), vec.end(), time);

    int fileIndex = -1;
    //check what index we got 
    if (iter != vec.end()) {
        if (iter != vec.begin()) {
            fileIndex = static_cast<int>(
                std::distance(vec.begin(), iter)
                ) - 1;
        }
        else {
            fileIndex = 0;
        }
    }
    else {
        fileIndex = static_cast<int>(vec.size()) - 1;
    }

    return fileIndex;
}

// Todo: handle signalIsSending, not only signalIsActive for the signal segments
bool RenderableSignals::isSignalActive(double currentTime, std::string signalStartTime, std::string signalEndTime, double lightTravelTime) {
    double startTimeInSeconds = SpiceManager::ref().ephemerisTimeFromDate(signalStartTime);
    double endTimeInSeconds = SpiceManager::ref().ephemerisTimeFromDate(signalEndTime) + lightTravelTime;

    if (startTimeInSeconds <= currentTime && endTimeInSeconds >= currentTime)
        return true;

    return false;
}

void RenderableSignals::extractData(std::unique_ptr<ghoul::Dictionary> &dictionary) {

    if (!SignalManager::extractMandatoryInfoFromDictionary(_identifier, dictionary)) {
        LERROR(fmt::format("{}: Did not manage to extract data.", _identifier));
    }
    else {
        LDEBUG(fmt::format("{}: Successfully read data.", _identifier));
    }
}

void RenderableSignals::pushSignalDataToVertexArray(SignalManager::Signal signal) {

    glm::vec4 color = { getStationColor(signal.dishName), 1.0 };
    glm::vec3 posStation = getPositionForGeocentricSceneGraphNode(signal.dishName.c_str());
    glm::vec3 posSpacecraft = getSuitablePrecisionPositionForSceneGraphNode(signal.spacecraft.c_str());
    double distance = getDistance(signal.dishName, signal.spacecraft);
    double timeSinceStart = signal.timeSinceStart;


    //fill the render array
    _vertexArray.push_back(posStation.x);
    _vertexArray.push_back(posStation.y);
    _vertexArray.push_back(posStation.z);

    _vertexArray.push_back(color.r);
    _vertexArray.push_back(color.g);
    _vertexArray.push_back(color.b);
    _vertexArray.push_back(color.a);
    // Todo: handle uplink and downlink differently
    _vertexArray.push_back(0.0);
    _vertexArray.push_back(timeSinceStart);

    _vertexArray.push_back(posSpacecraft.x);
    _vertexArray.push_back(posSpacecraft.y);
    _vertexArray.push_back(posSpacecraft.z);

    _vertexArray.push_back(color.r);
    _vertexArray.push_back(color.g);
    _vertexArray.push_back(color.b);
    _vertexArray.push_back(color.a);

    // Todo: handle uplink and downlink differently
    _vertexArray.push_back(distance);
    _vertexArray.push_back(timeSinceStart);
}

/* Since our station dishes have a static translation from Earth, we
* can get their local translation. The reason to handle it differently
* compared to the spacecrafts is to keep an exact render position
* for the station line ends even when the focusNode is Earth. */
glm::dvec3 RenderableSignals::getCoordinatePosFromFocusNode(SceneGraphNode* node) {

    glm::dvec3 nodePos = node->worldPosition();
    glm::dvec3 focusNodePos = _focusNode->worldPosition();

    glm::dvec3 diff = glm::vec3(nodePos.x - focusNodePos.x, nodePos.y - focusNodePos.y,
        nodePos.z - focusNodePos.z);

    return diff;
}

glm::vec3 RenderableSignals::getSuitablePrecisionPositionForSceneGraphNode(std::string id) {

    glm::vec3 position;

    if (global::renderEngine.scene()->sceneGraphNode(id)) {
        SceneGraphNode* spacecraftNode = global::renderEngine.scene()->sceneGraphNode(id);
        position = getCoordinatePosFromFocusNode(spacecraftNode);
    }
    else {
        LERROR(fmt::format("No scengraphnode found for the spacecraft {}", id));
    }

    return position;
}

glm::vec3 RenderableSignals::getPositionForGeocentricSceneGraphNode(const char* id) {

    glm::dvec3 position;

    if (global::renderEngine.scene()->sceneGraphNode(id)) {
        position = global::renderEngine.scene()->sceneGraphNode(id)->position();
    }
    else {
        LERROR(fmt::format("No scengraphnode found for the station dish {}, drawing line from center of Earth", id));
        position = glm::vec3(0, 0, 0);
    }

    return position;
}

glm::vec3 RenderableSignals::getStationColor(std::string dishidentifier) {

    glm::vec3 color(0.0f, 0.0f, 1.0f);
    std::string site;

    try {
        site = _stationToSite.at(dishidentifier);
    }
    catch (const std::exception& e) {
        LERROR(fmt::format("Station {} has no site location.", dishidentifier));
    }

    int siteIndex = _siteToIndex.at(site);
    color = _siteColors[siteIndex]->value();

    return color;
}

float RenderableSignals::getDistance(std::string nodeIdA, std::string nodeIdB) {

    glm::vec3 posA = global::renderEngine.scene()->sceneGraphNode(nodeIdA)->worldPosition();
    glm::vec3 posB = global::renderEngine.scene()->sceneGraphNode(nodeIdB)->worldPosition();

    return glm::distance(posA, posB);
}

} // namespace openspace