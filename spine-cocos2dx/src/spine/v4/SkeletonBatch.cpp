/******************************************************************************
 * Spine Runtimes License Agreement
 * Last updated May 1, 2019. Replaces all prior versions.
 *
 * Copyright (c) 2013-2019, Esoteric Software LLC
 *
 * Integration of the Spine Runtimes into software or otherwise creating
 * derivative works of the Spine Runtimes is permitted under the terms and
 * conditions of Section 2 of the Spine Editor License Agreement:
 * http://esotericsoftware.com/spine-editor-license
 *
 * Otherwise, it is permitted to integrate the Spine Runtimes into software
 * or otherwise create derivative works of the Spine Runtimes (collectively,
 * "Products"), provided that each user of the Products must obtain their own
 * Spine Editor license and redistribution of the Products in any form must
 * include this license and copyright notice.
 *
 * THIS SOFTWARE IS PROVIDED BY ESOTERIC SOFTWARE LLC "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL ESOTERIC SOFTWARE LLC BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES, BUSINESS
 * INTERRUPTION, OR LOSS OF USE, DATA, OR PROFITS) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#include <spine/spine-cocos2dx.h>
#if COCOS2D_VERSION >= 0x00040000

#include <spine/Extension.h>
#include <algorithm>

USING_NS_CC;
#define EVENT_AFTER_DRAW_RESET_POSITION "director_after_draw"
using std::max;
#define INITIAL_SIZE (10000)

#include "renderer/ccShaders.h"
#include "renderer/backend/Device.h"

#include "renderer/backend/opengl/BufferGL.h"
#include "renderer/backend/opengl/TextureGL.h"

namespace spine {

static SkeletonBatch* instance = nullptr;

SkeletonBatch* SkeletonBatch::getInstance () {
	if (!instance) instance = new SkeletonBatch();
	return instance;
}

void SkeletonBatch::destroyInstance () {
	if (instance) {
		delete instance;
		instance = nullptr;
	}
}

SkeletonBatch::SkeletonBatch () {

    //auto program = backend::Device::getInstance()->newProgram(positionTextureColor_vert, positionTextureColor_frag);
    auto propram = backend::Program::getBuiltinProgram(cocos2d::backend::ProgramType::POSITION_TEXTURE_COLOR);
    _programState = std::make_shared<backend::ProgramState>(propram);
    
    //program->autorelease();

    auto vertexLayout = _programState->getVertexLayout();

    vertexLayout->setAttribute("a_position", 0, backend::VertexFormat::FLOAT3, offsetof(V3F_C4B_T2F, vertices), false);
    vertexLayout->setAttribute("a_color", 2, backend::VertexFormat::UBYTE4, offsetof(V3F_C4B_T2F, colors), true);
    vertexLayout->setAttribute("a_texCoord", 1, backend::VertexFormat::FLOAT2, offsetof(V3F_C4B_T2F, texCoords), false);
    vertexLayout->setLayout(sizeof(_vertices[0]));


    _locMVP = _programState->getUniformLocation("u_MVPMatrix");
    _locTexture = _programState->getUniformLocation("u_texture");

    for (unsigned int i = 0; i < INITIAL_SIZE; i++) {
        _commandsPool.push_back(createNewTrianglesCommand());
    }
    reset();
    // callback after drawing is finished so we can clear out the batch state
    // for the next frame
    Director::getInstance()->getEventDispatcher()->addCustomEventListener(EVENT_AFTER_DRAW_RESET_POSITION, [this](EventCustom* eventCustom) {
        this->update(0);
        });;
}

SkeletonBatch::~SkeletonBatch () {
	Director::getInstance()->getEventDispatcher()->removeCustomEventListeners(EVENT_AFTER_DRAW_RESET_POSITION);

	for (unsigned int i = 0; i < _commandsPool.size(); i++) {
        CC_SAFE_RELEASE(_commandsPool[i]->getPipelineDescriptor().programState);
		delete _commandsPool[i];
		_commandsPool[i] = nullptr;
	}
}

void SkeletonBatch::update (float delta) {
	reset();
}

cocos2d::V3F_C4B_T2F* SkeletonBatch::allocateVertices(uint32_t numVertices) {
	if (_vertices.size() - _numVertices < numVertices) {
		cocos2d::V3F_C4B_T2F* oldData = _vertices.data();
		_vertices.resize((_vertices.size() + numVertices) * 2 + 1);
		cocos2d::V3F_C4B_T2F* newData = _vertices.data();
		for (uint32_t i = 0; i < this->_nextFreeCommand; i++) {
#if !SPINE_USE_CUSTOM_COMMAND
			TrianglesCommand* command = _commandsPool[i];
			cocos2d::TrianglesCommand::Triangles& triangles = (cocos2d::TrianglesCommand::Triangles&)command->getTriangles();
			triangles.verts = newData + (triangles.verts - oldData);
#else
            CustomCommand* command = _commandsPool[i];
            auto* buffer = command->getVertexBuffer();
            //cocos2d::V3F_C4B_T2F* current = (cocos2d::V3F_C4B_T2F *)((cocos2d::backend::BufferGL*)buffer)->getData();
            command->updateVertexBuffer(newData, buffer->getSize());
#endif
		}
	}

	cocos2d::V3F_C4B_T2F* vertices = _vertices.data() + _numVertices;
	_numVertices += numVertices;
	return vertices;
}

void SkeletonBatch::deallocateVertices(uint32_t numVertices) {
	_numVertices -= numVertices;
}


unsigned short* SkeletonBatch::allocateIndices(uint32_t numIndices) {
	if (_indices.getCapacity() - _indices.size() < numIndices) {
		unsigned short* oldData = _indices.buffer();
		int oldSize = _indices.size();
		_indices.ensureCapacity(_indices.size() + numIndices);
		unsigned short* newData = _indices.buffer();
		for (uint32_t i = 0; i < this->_nextFreeCommand; i++) {
#if !SPINE_USE_CUSTOM_COMMAND
			TrianglesCommand* command = _commandsPool[i];
			cocos2d::TrianglesCommand::Triangles& triangles = (cocos2d::TrianglesCommand::Triangles&)command->getTriangles();
			if (triangles.indices >= oldData && triangles.indices < oldData + oldSize) {
				triangles.indices = newData + (triangles.indices - oldData);
			}
#else
            CustomCommand* command = _commandsPool[i];
            auto* buffer = command->getIndexBuffer();
            //unsigned short* current = (unsigned short*)((cocos2d::backend::BufferGL*)buffer)->getData();
           // if (current >= oldData && current < oldData + oldSize) {
            //    current = newData + (current- oldData);
                command->updateIndexBuffer(newData, buffer->getSize());
            //}

#endif
		}
	}

	unsigned short* indices = _indices.buffer() + _indices.size();
	_indices.setSize(_indices.size() + numIndices, 0);
	return indices;
}

void SkeletonBatch::deallocateIndices(uint32_t numIndices) {
	_indices.setSize(_indices.size() - numIndices, 0);
}


SkeletonBatch::BatchCommand* SkeletonBatch::addCommand(cocos2d::Renderer* renderer, float globalOrder, cocos2d::Texture2D* texture, cocos2d::BlendFunc blendType, const cocos2d::TrianglesCommand::Triangles& triangles, const cocos2d::Mat4& mv, uint32_t flags) {
    SkeletonBatch::BatchCommand* command = nextFreeCommand();
    const cocos2d::Mat4& projectionMat = Director::getInstance()->getMatrix(MATRIX_STACK_TYPE::MATRIX_STACK_PROJECTION);

    auto programState = command->getPipelineDescriptor().programState;
    CCASSERT(programState, "programState should not be null");

#if SPINE_USE_CUSTOM_COMMAND
#if SPINE_RELOAD_VERTEX
    programState->setUniform(_locMVP, Mat4::IDENTITY.m, sizeof(Mat4::IDENTITY.m));
#else
    Mat4 tmp = projectionMat * mv;
    programState->setUniform(_locMVP, tmp.m, sizeof(tmp.m));
#endif
#else
    programState->setUniform(_locMVP, projectionMat.m, sizeof(projectionMat.m));
#endif
    // programState->setUniform(_locMVP, Mat4::IDENTITY.m, sizeof(Mat4::IDENTITY.m));
    programState->setTexture(_locTexture, 0, texture->getBackendTexture());

    //int slot = 3;
    //programState->setUniform(_locTexture, &slot, sizeof(slot));

    //glActiveTexture(GL_TEXTURE0 + slot);
    //glBindTexture(GL_TEXTURE_2D, ((backend::Texture2DGL*)texture->getBackendTexture())->getHandler());


    //for (int i = 0; i < triangles.vertCount; i++)
    //{
    //    auto& v = triangles.verts[i].vertices;
    //    v.x = CCRANDOM_0_1() * 2.0 - 1.0;
    //    v.y = CCRANDOM_0_1() * 2.0 - 1.0;
    //}
    //Mat4 tmp = mv;
    //tmp.multiply(mv);
    //auto output = projectionMat * mv;
#if SPINE_USE_CUSTOM_COMMAND

#if SPINE_ADVOID_RECREATE_BUFFER
    if (!command->getIndexBuffer() || command->getIndexBuffer()->getSize() != triangles.indexCount * sizeof(triangles.indices[0]))
    {
        command->createIndexBuffer(cocos2d::CustomCommand::IndexFormat::U_SHORT, triangles.indexCount, cocos2d::CustomCommand::BufferUsage::DYNAMIC);
    }
    if (!command->getVertexBuffer() || command->getVertexBuffer()->getSize() != triangles.vertCount * sizeof(triangles.verts[0]))
    {
        command->createVertexBuffer(sizeof(triangles.verts[0]), triangles.vertCount, cocos2d::CustomCommand::BufferUsage::DYNAMIC);
    }
#else
    command->createVertexBuffer(sizeof(triangles.verts[0]), triangles.vertCount, cocos2d::CustomCommand::BufferUsage::DYNAMIC);
    command->createIndexBuffer(cocos2d::CustomCommand::IndexFormat::U_SHORT, triangles.indexCount, cocos2d::CustomCommand::BufferUsage::DYNAMIC);
#endif

    command->updateIndexBuffer(triangles.indices, triangles.indexCount * sizeof(triangles.indices[0]));
    command->updateVertexBuffer(triangles.verts, triangles.vertCount * sizeof(triangles.verts[0]));

    command->init(globalOrder,  blendType);
#else
    command->init(globalOrder, texture, blendType, triangles, mv, flags);
#endif
    renderer->addCommand(command);
	return command;
}

void SkeletonBatch::reset() {
	_nextFreeCommand = 0;
	_numVertices = 0;
	_indices.setSize(0, 0);
}

SkeletonBatch::BatchCommand* SkeletonBatch::nextFreeCommand() {
    if (_commandsPool.size() <= _nextFreeCommand) {
        unsigned int newSize = _commandsPool.size() * 2 + 1;
        for (int i = _commandsPool.size(); i < newSize; i++) {
            _commandsPool.push_back(createNewTrianglesCommand());
        }
    }
    auto* command = _commandsPool[_nextFreeCommand++];
    auto& pipelineDescriptor = command->getPipelineDescriptor();
    if (pipelineDescriptor.programState == nullptr)
    {
        CCASSERT(_programState, "programState should not be null");
        pipelineDescriptor.programState = _programState->clone();
    }
    return command;
}

SkeletonBatch::BatchCommand*SkeletonBatch::createNewTrianglesCommand() {
    auto* command = new BatchCommand();
    //command->setSkipBatching(true);
    return command;
}
}

#endif
