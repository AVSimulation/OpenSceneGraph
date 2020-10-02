/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2006 Robert Osfield 
 *
 * This library is open source and may be redistributed and/or modified under  
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or 
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * OpenSceneGraph Public License for more details.
*/

/* Note, elements of PixelBufferEGL have used Prodcer/RenderSurface_EGL.cpp as both
 * a guide to use of EGL/GLX and copiying directly in the case of setBorder().
 * These elements are license under OSGPL as above, with Copyright (C) 2001-2004  Don Burns.
 */

#include <osgViewer/api/EGL/PixelBufferEGL>
#include <osgViewer/api/EGL/GraphicsWindowEGL>
#include <osg/GLExtensions>


#include <unistd.h>

using namespace osgViewer;

PixelBufferEGL::PixelBufferEGL(osg::GraphicsContext::Traits* traits)
  : _valid(false),
    _pbuffer(0),
    _initialized(false),
    _realized(false)
{
    _traits = traits;

    init();
    
    if (valid())
    {
        setState( new osg::State );
        getState()->setGraphicsContext(this);

        if (_traits.valid() && _traits->sharedContext.get())
        {
            getState()->setContextID( _traits->sharedContext->getState()->getContextID() );
            incrementContextIDUsageCount( getState()->getContextID() );   
        }
        else
        {
            getState()->setContextID( osg::GraphicsContext::createNewContextID() );
        }

    }
}

PixelBufferEGL::~PixelBufferEGL()
{
    close(true);
}

bool PixelBufferEGL::createVisualInfo()
{
	typedef std::vector<int> Attributes;
	Attributes attributes;

	attributes.push_back(EGL_SURFACE_TYPE);
	attributes.push_back(EGL_PBUFFER_BIT);

// TODO        if (_traits->doubleBuffer) attributes.push_back(GLX_DOUBLEBUFFER);
// TODO        if (_traits->quadBufferStereo) attributes.push_back(GLX_STEREO);

	attributes.push_back(EGL_RED_SIZE); attributes.push_back(_traits->red);
	attributes.push_back(EGL_GREEN_SIZE); attributes.push_back(_traits->green);
	attributes.push_back(EGL_BLUE_SIZE); attributes.push_back(_traits->blue);
	attributes.push_back(EGL_DEPTH_SIZE); attributes.push_back(_traits->depth);

	if (_traits->alpha) { attributes.push_back(EGL_ALPHA_SIZE); attributes.push_back(_traits->alpha); }

	if (_traits->stencil) { attributes.push_back(EGL_STENCIL_SIZE); attributes.push_back(_traits->stencil); }

#if defined(EGL_SAMPLE_BUFFERS) && defined (EGL_SAMPLES)

	if (_traits->sampleBuffers) { attributes.push_back(EGL_SAMPLE_BUFFERS); attributes.push_back(_traits->sampleBuffers); }
	if (_traits->samples) { attributes.push_back(EGL_SAMPLES); attributes.push_back(_traits->samples); }

#endif

	attributes.push_back(EGL_NONE);

	eglChooseConfig(_display, &(attributes.front()), &_eglCfg, 1, &_numConfigs);

    return _eglCfg != 0;
}

void PixelBufferEGL::init()
{
    if (_initialized) return;

    if (!_traits)
    {
        _valid = false;
        return;
    }

    _display = openDisplay();

    if (!_display)
    {
        OSG_NOTICE<<"Error: Unable to open EGL display \"" << checkEGLError("DisplayCreation") << "\"."<<std::endl;
        _valid = false;
        return;
    }

    EGLint eglMajorVersion, eglMinorVersion;
    if (!eglInitialize(_display, &eglMajorVersion, &eglMinorVersion))
    {
	OSG_NOTICE<<"GraphicsWindowEGL::init() - eglInitialize() failed."<<std::endl;
	eglTerminate( _display );
	_display = 0;
	_valid = false;
	return;
    }

    OSG_NOTICE<<"GraphicsWindowEGL::init() - eglInitialize() succeded eglMajorVersion="<<eglMajorVersion<<" iMinorVersion="<<eglMinorVersion<<std::endl;

    if (!createVisualInfo())
    {
        _traits->red /= 2;
        _traits->green /= 2;
        _traits->blue /= 2;
        _traits->alpha /= 2;
        _traits->depth /= 2;

        OSG_INFO<<"Relaxing traits"<<std::endl;

        if (!createVisualInfo())
        {
            OSG_NOTICE<<"Error: Not able to create requested visual." << std::endl;
            eglTerminate( _display );
            _display = 0;
            _valid = false;
            return;
        }
    }

    // get any shared GLX contexts
    GraphicsHandleEGL* graphicsHandleEGL = dynamic_cast<GraphicsHandleEGL*>(_traits->sharedContext.get());
    Context sharedContext = graphicsHandleEGL ? graphicsHandleEGL->getContext() : 0;

	eglBindAPI(EGL_OPENGL_API);

	typedef std::vector<int> Attributes;
	Attributes attributes;

	attributes.push_back(EGL_WIDTH);
	attributes.push_back(_traits->width);
	attributes.push_back(EGL_HEIGHT);
	attributes.push_back(_traits->height);
	attributes.push_back(EGL_NONE);
	
	_pbuffer = eglCreatePbufferSurface(_display, _eglCfg, &(attributes.front()));
	if (_pbuffer == EGL_NO_SURFACE)
	{
		OSG_NOTICE<<"GraphicsWindowEGL::init() - eglCreateWindowSurface(..) failed."<<std::endl;
		eglTerminate( _display );
		_valid = false;
		_display = 0;
		return;
	}

	_context = eglCreateContext(_display, _eglCfg, sharedContext, NULL);
	if (_context == EGL_NO_CONTEXT)
	{
		OSG_NOTICE<<"GraphicsWindowEGL::init() - eglCreateContext(..) failed."<<std::endl;
		eglTerminate( _display );
		_valid = false;
		_display = 0;
		return;
	}

	_initialized = true;
	_valid = true;

	checkEGLError("after eglCreateContext()");
}




bool PixelBufferEGL::realizeImplementation()
{
    if (_realized)
    {
        OSG_NOTICE<<"PixelBufferEGL::realizeImplementation() Already realized"<<std::endl;
        return true;
    }

    if (!_initialized) init();
    
    if (!_initialized) return false;

    _realized = true;

    return true;
}

bool PixelBufferEGL::makeCurrentImplementation()
{
    if (!_realized)
    {
        OSG_NOTICE<<"Warning: GraphicsWindow not realized, cannot do makeCurrent."<<std::endl;
        return false;
    }

    bool result = eglMakeCurrent(_display, _pbuffer, _pbuffer, _context)==EGL_TRUE;
    checkEGLError("after eglMakeCurrent()");
    return result;
}

bool PixelBufferEGL::makeContextCurrentImplementation(osg::GraphicsContext* /*readContext*/)
{
    // OSG_NOTICE<<"PixelBufferEGL::makeContextCurrentImplementation() not implementation yet."<<std::endl;
    return makeCurrentImplementation();
}


bool PixelBufferEGL::releaseContextImplementation()
{
    if (!_realized)
    {
        OSG_NOTICE<<"Warning: GraphicsWindow not realized, cannot do release context."<<std::endl;
        return false;
    }

    bool result = eglMakeCurrent( _display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT )==EGL_TRUE;
    checkEGLError("after eglMakeCurrent() release");
    return result;
}

void PixelBufferEGL::closeImplementation()
{
    // OSG_NOTICE<<"Closing GraphicsWindowEGL"<<std::endl;

    if (_display)
    {
        if (_context)
			eglDestroyContext( _display, _context );
    }

    _context = 0;

    if (_display)
    {
		eglTerminate( _display );
        _display = 0;
    }

    _initialized = false;
    _realized = false;
    _valid = false;
}

void PixelBufferEGL::swapBuffersImplementation()
{
    if (!_realized) return;

    // OSG_NOTICE<<"swapBuffersImplementation "<<this<<" "<<OpenThreads::Thread::CurrentThread()<<std::endl;

    eglSwapBuffers( _display, _pbuffer );
    checkEGLError("after eglSwapBuffers()");
}

void PixelBufferEGL::bindPBufferToTextureImplementation(GLenum buffer)
{
    OSG_NOTICE<<"PixelBufferEGL::bindPBufferToTextureImplementation() not implementation yet."<<std::endl;
}
