//     Copyright 2012, Kay Hayen, mailto:kayhayen@gmx.de
//
//     Part of "Nuitka", an optimizing Python compiler that is compatible and
//     integrates with CPython, but also works on its own.
//
//     If you submit patches or make the software available to licensors of
//     this software in either form, you automatically them grant them a
//     license for your part of the code under "Apache License 2.0" unless you
//     choose to remove this notice.
//
//     Kay Hayen uses the right to license his code under only GPL version 3,
//     to discourage a fork of Nuitka before it is "finished". He will later
//     make a new "Nuitka" release fully under "Apache License 2.0".
//
//     This program is free software: you can redistribute it and/or modify
//     it under the terms of the GNU General Public License as published by
//     the Free Software Foundation, version 3 of the License.
//
//     This program is distributed in the hope that it will be useful,
//     but WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//     GNU General Public License for more details.
//
//     You should have received a copy of the GNU General Public License
//     along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//     Please leave the whole of this copyright notice intact.
//
#ifndef __NUITKA_FRAMEGUARDS_H__
#define __NUITKA_FRAMEGUARDS_H__

inline static void assertCodeObject( PyCodeObject *code_object )
{
    assertObject( (PyObject *)code_object );
}

inline static void assertFrameObject( PyFrameObject *frame_object )
{
    assertObject( (PyObject *)frame_object );
    assertCodeObject( frame_object->f_code );
}

NUITKA_MAY_BE_UNUSED static PyFrameObject *INCREASE_REFCOUNT( PyFrameObject *frame_object )
{
    assertFrameObject( frame_object );

    Py_INCREF( frame_object );
    return frame_object;
}

NUITKA_MAY_BE_UNUSED static bool isFrameUnusable( PyFrameObject *frame_object )
{
    return
        // Never used.
        frame_object == NULL ||
        // Still in use
        Py_REFCNT( frame_object ) > 1 ||
        // Last used by another thread (TODO: Could just set it when re-using)
        frame_object->f_tstate != PyThreadState_GET() ||
        // Was detached from (TODO: When detaching, can't we just have another
        // frame guard instead)
        frame_object->f_back != NULL;
}

inline static void popFrameStack( void )
{
    PyThreadState *tstate = PyThreadState_GET();

    PyFrameObject *old = tstate->frame;

#if _DEBUG_REFRAME
    printf( "Taking off frame %s %s\n", PyString_AsString( PyObject_Str( (PyObject *)old ) ), PyString_AsString( PyObject_Str( (PyObject *)old->f_code ) ) );
#endif

    tstate->frame = old->f_back;

#if _DEBUG_REFRAME
    printf( "Now at top frame %s %s\n", PyString_AsString( PyObject_Str( (PyObject *)tstate->frame ) ), PyString_AsString( PyObject_Str( (PyObject *)tstate->frame->f_code ) ) );
#endif
}

inline static void pushFrameStack( PyFrameObject *frame_object )
{
    PyThreadState *tstate = PyThreadState_GET();

    // Look at current frame.
    PyFrameObject *old = tstate->frame;

#if _DEBUG_REFRAME
    printf( "Upstacking to frame %s %s\n", PyString_AsString( PyObject_Str( (PyObject *)old ) ), PyString_AsString( PyObject_Str( (PyObject *)old->f_code ) ) );
#endif

    // No recursion allowed of course, assert against it.
    assert( old != frame_object );

    // Push the new frame as the currently active one.
    tstate->frame = frame_object;

    // We don't allow touching cached frame objects where this is not true.
    assert( frame_object->f_back == NULL );

    if ( old != NULL )
    {
        assertFrameObject( old );
        frame_object->f_back = INCREASE_REFCOUNT( old );
    }

#if _DEBUG_REFRAME
    printf( "Now at top frame %s %s\n", PyString_AsString( PyObject_Str( (PyObject *)tstate->frame ) ), PyString_AsString( PyObject_Str( (PyObject *)tstate->frame->f_code ) ) );
#endif
}

#if _DEBUG_REFRAME
static inline void dumpFrameStack( void )
{
    PyFrameObject *current = PyThreadState_GET()->frame;
    int total = 0;

    while( current )
    {
        total++;
        current = current->f_back;
    }

    current = PyThreadState_GET()->frame;

    puts( ">--------->" );

    while( current )
    {
        printf( "Frame stack %d: %s %s\n", total--, PyString_AsString( PyObject_Str( (PyObject *)current ) ), PyString_AsString( PyObject_Str( (PyObject *)current->f_code ) ) );

        current = current->f_back;
    }

    puts( ">---------<" );
}
#endif

// Make a replacement for the current top frame, that we again own exclusively enough so
// that the line numbers are detached.
extern PyFrameObject *detachCurrentFrame();

class FrameGuard
{
public:
    FrameGuard( PyFrameObject *frame_object )
    {
        assertFrameObject( frame_object );

        // Remember it.
        this->frame_object = frame_object;

        // Push the new frame as the currently active one.
        pushFrameStack( frame_object );

        // Keep the frame object alive for this C++ objects live time.
        Py_INCREF( frame_object );

#if _DEBUG_REFRAME
        dumpFrameStack();
#endif
    }

    ~FrameGuard()
    {
        // Our frame should be on top.
        assert( PyThreadState_GET()->frame == this->frame_object );

        // Put the previous frame on top instead.
        popFrameStack();

        assert( PyThreadState_GET()->frame != this->frame_object );

        // Should still be good.
        assertFrameObject( this->frame_object );

        // Release the back reference immediately.
        Py_XDECREF( this->frame_object->f_back );
        this->frame_object->f_back = NULL;

        // Now release our frame object reference.
        Py_DECREF( this->frame_object );
    }

    PyFrameObject *getFrame() const
    {
        return INCREASE_REFCOUNT( this->frame_object );
    }

    // Use this to set the current line of the frame
    void setLineNumber( int lineno ) const
    {
        assertFrameObject( this->frame_object );
        assert( lineno >= 1 );

        // Make sure f_lineno is the actually used information.
        assert( this->frame_object->f_trace == Py_None );

        this->frame_object->f_lineno = lineno;
    }

    // Replace the frame object by a newer one.
    void detachFrame( void )
    {
        // Our old frame should be on top.
        assert( PyThreadState_GET()->frame == this->frame_object );

        this->frame_object = detachCurrentFrame();

        // Our new frame should be on top.
        assert( PyThreadState_GET()->frame == this->frame_object );
    }

private:
    PyFrameObject *frame_object;
};

class FrameGuardLight
{
public:
    FrameGuardLight( PyFrameObject **frame_ptr )
    {
        assertFrameObject( *frame_ptr );

        // Remember it.
        this->frame_ptr = frame_ptr;
    }

    ~FrameGuardLight()
    {
        // Should still be good.
        assertFrameObject( *this->frame_ptr );
    }

    PyFrameObject *getFrame() const
    {
        return INCREASE_REFCOUNT( *this->frame_ptr );
    }

    // Use this to set the current line of the frame
    void setLineNumber( int lineno ) const
    {
        assertFrameObject( *this->frame_ptr );
        assert( lineno >= 1 );

        // Make sure f_lineno is the actually used information.
        assert( (*this->frame_ptr)->f_trace == Py_None );

        (*this->frame_ptr)->f_lineno = lineno;
    }

    // Replace the frame object by a newer one.
    void detachFrame( void )
    {
        // Our old frame should be on top.
        assert( PyThreadState_GET()->frame == *this->frame_ptr );

        *this->frame_ptr = detachCurrentFrame();

        // Our new frame should be on top.
        assert( PyThreadState_GET()->frame == *this->frame_ptr );
    }


private:
    PyFrameObject **frame_ptr;
};


#endif
