/*==============================================================================

  Copyright 2010 Rowdy Dog Software.

  This file is part of TinyTuner.

  TinyTuner is free software: you can redistribute it and/or modify it under
  the terms of the GNU Lesser General Public License as published by the Free
  Software Foundation, either version 3 of the License, or (at your option)
  any later version.

  TinyTuner is distributed in the hope that it will be useful, but WITHOUT 
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
  License for more details.

  You should have received a copy of the GNU Lesser General Public License 
  along with TinyTuner.  If not, see <http://www.gnu.org/licenses/>.

  Inspired by the work of oPossum (Kevin Timmerman)...
  http://forums.adafruit.com/viewtopic.php?t=5078

==============================================================================*/

#ifndef TinyTuner_h
#define TinyTuner_h

#include <avr/io.h>
#include <avr/interrupt.h>
#include <inttypes.h>

template
  <
    uint8_t CAL_REG,
    uint8_t CAL_BIT
  >
class TinyTuner
{
public:

  typedef enum
  {
    sFirstPass,
    sBigSteps,
    sConfirm,
    sFinished
  }
  state_t;

  typedef enum
  {
    pLeft,
    pThis,
    pRight,
    pMax
  }
  position_t;

  typedef struct
  {
    int16_t     OsccalValue;
    uint16_t    NineBitTime;
    int16_t     Error;
    uint8_t     ConfirmCount;
    uint16_t    ConfirmNineBitTime;
  }
  info_t;

private:

  state_t _state;

  position_t _position;

  info_t _info[pMax];

public:

  TinyTuner()
  {
    _state = sFirstPass;
  }

  bool update( void )
  {
    // Time the 'x'
    uint16_t nbt = TimeNineBits();

    // Calculate the number of clock cycles spent in TimeNineBits
    int16_t clocks = (nbt-1)*5 + 5;

    // Calculate the difference between the actual number of cycles spent in TimeNineBits and the expected number of cycles
    int16_t error = clocks - 7500;

    if ( _state == sFirstPass )
    {
      _info[pLeft].OsccalValue  = -1;
      _info[pThis].OsccalValue  = OSCCAL & 0x7F;
      _info[pRight].OsccalValue = 0x80;
      _position = pThis;
      _state = sBigSteps;
    }

    if ( _state == sConfirm )
    {
      uint16_t delta;

      if ( nbt > _info[_position].NineBitTime )
      {
        delta = nbt - _info[_position].NineBitTime;
      }
      else
      {
        delta = _info[_position].NineBitTime - nbt;
      }
      
      _info[_position].NineBitTime = nbt;
      
      if ( (delta <= 2) || (_info[_position].ConfirmCount == 0) )
      {
        ++_info[_position].ConfirmCount;
        _info[_position].ConfirmNineBitTime += nbt;
        
        if ( _info[_position].ConfirmCount >= 3 )
        {
          for ( _position=pLeft; _position < pMax; _position=(position_t)(_position+1) )
          {
            if ( _info[_position].ConfirmCount < 3 )
            {
              break;
            }
          }
          if ( _position == pMax )
          {
            FindBest();
            _state = sFinished;
          }
          else
          {
          }
        }
      }
      else
      {
        _info[_position].ConfirmCount = 0;
        _info[_position].ConfirmNineBitTime = 0;
      }
    }

    if ( _state == sBigSteps )
    {
      int8_t nobs = NumberOfBigSteps( error );

      if ( error < 0 )
      {
        _info[pLeft].OsccalValue = _info[pThis].OsccalValue;
        _info[pLeft].NineBitTime = nbt;
        _info[pLeft].Error = -error;
        _info[pThis].OsccalValue += nobs;

        if ( _info[pThis].OsccalValue >= _info[pRight].OsccalValue )
        {
          _info[pThis].OsccalValue = _info[pRight].OsccalValue - 1;

          if ( _info[pThis].OsccalValue <= _info[pLeft].OsccalValue )
          {
            // fix? Do something special about the greater-than case?  If everything else is correct, it should never occur.
            if ( _info[pLeft].OsccalValue + 1 == _info[pRight].OsccalValue )
            {
              TransitionToConfirm();
            }
            else
            {
              _info[pThis].OsccalValue = _info[pLeft].OsccalValue + 1;
            }
          }
        }
      }
      else
      {
        _info[pRight].OsccalValue = _info[pThis].OsccalValue;
        _info[pRight].NineBitTime = nbt;
        _info[pRight].Error = +error;
        _info[pThis].OsccalValue -= nobs;

        if ( _info[pThis].OsccalValue <= _info[pLeft].OsccalValue )
        {
          _info[pThis].OsccalValue = _info[pLeft].OsccalValue + 1;

          if ( _info[pThis].OsccalValue >= _info[pRight].OsccalValue )
          {
            // fix? Do something special about the less-than case?  If everything else is correct, it should never occur.
            if ( _info[pLeft].OsccalValue + 1 == _info[pRight].OsccalValue )
            {
              TransitionToConfirm();
            }
            else
            {
              _info[pThis].OsccalValue = _info[pRight].OsccalValue - 1;
            }
          }
        }
      }
    }

    AdjustOSCCAL( (uint8_t)(_info[_position].OsccalValue) );

    if ( _state == sFinished )
    {
      return( false );
    }

    return( true );
  }

public:

  state_t getState( void )
  {
    return( _state );
  }

protected:

  void FindBest( void )
  {
    uint16_t nbt;
    int16_t clocks;
    int16_t error;
    position_t position;
    int16_t BestError;
    
    BestError = 0x7FFF;
    
    for ( position=pLeft; position < pMax; position=(position_t)(position+1) )
    {
      nbt = _info[position].ConfirmNineBitTime / _info[position].ConfirmCount;
      clocks = (nbt-1)*5 + 5;
      error = clocks - 7500;

      if ( error < 0 )
      {
        error = -error;
      }
      if ( error < BestError )
      {
        BestError = error;
        _position = position;
      }
    }
  }

  void TransitionToConfirm( void )
  {
    if ( _info[pLeft].Error < _info[pRight].Error )
    {
      _info[pThis].OsccalValue = _info[pLeft].OsccalValue;
      _info[pThis].NineBitTime = _info[pLeft].NineBitTime;
      _info[pThis].Error = _info[pLeft].Error;
      
      _info[pLeft].OsccalValue = _info[pThis].OsccalValue - 1;
      _info[pLeft].ConfirmCount = 0;
      _info[pLeft].ConfirmNineBitTime = 0;
      
      _info[pThis].ConfirmCount = 1;
      _info[pThis].ConfirmNineBitTime = _info[pThis].NineBitTime;
      
      _info[pRight].ConfirmCount = 1;
      _info[pRight].ConfirmNineBitTime = _info[pRight].NineBitTime;
    }
    else
    {
      _info[pThis].OsccalValue = _info[pRight].OsccalValue;
      _info[pThis].NineBitTime = _info[pRight].NineBitTime;
      _info[pThis].Error = _info[pRight].Error;

      _info[pLeft].ConfirmCount = 1;
      _info[pLeft].ConfirmNineBitTime = _info[pLeft].NineBitTime;

      _info[pThis].ConfirmCount = 1;
      _info[pThis].ConfirmNineBitTime = _info[pThis].NineBitTime;

      _info[pRight].OsccalValue = _info[pThis].OsccalValue + 1;
      _info[pRight].ConfirmCount = 0;
      _info[pRight].ConfirmNineBitTime = 0;
    }
    _state = sConfirm;
  }

public:

  static int8_t NumberOfBigSteps( int16_t error )
  {
    error = error / 100;
    
    switch ( error )
    {
      case -7:  return( 20 );
      case -6:  return( 17 );
      case -5:  return( 15 );
      case -4:  return( 12 );
      case -3:  return(  9 );
      case -2:  return(  6 );
      case -1:  return(  3 );
      case  0:  return(  1 );
      case +1:  return(  3 );
      case +2:  return(  6 );
      case +3:  return(  9 );
      case +4:  return( 11 );
      case +5:  return( 13 );
      case +6:  return( 15 );
      case +7:  return( 17 );
    }
    return( error < 0 ? 21 : 18 );
  }

  static void AdjustOSCCAL( uint8_t NewValue )
  {
    uint8_t Temp;
    uint8_t Value;
    uint8_t Range;
    uint8_t Delta;

    Temp = OSCCAL;
    
    Value = Temp & 0x7F;
    Range = Temp & 0x80;
    
    if ( NewValue < Value )
    {
      while ( NewValue != Value )
      {
        --Value;
        OSCCAL = Range | Value;
      }
    }
    else if ( NewValue > Value )
    {
      while ( NewValue != Value )
      {
        ++Value;
        OSCCAL = Range | Value;
      }
    }
  }

  static uint16_t TimeNineBits( void )
  {
    // We need a fast (8 MHz) clock to maximize the accuracy
    uint8_t ClockDivisor = CLKPR;
    cli();
    CLKPR = _BV(CLKPCE);
    CLKPR = (0<<CLKPS3) | (0<<CLKPS2) | (0<<CLKPS1) | (0<<CLKPS0);
    sei();
    
    uint16_t Temp = 0;

    // lowercase 'x' on the wire...
    // ...1111111111 0 0001 1110 1 111111111...
    
    asm volatile
    (
    // Wait for a start bit
    "L%=wfsb: "
      "sbic  %[calreg], %[calbit]"              "\n\t"
      "rjmp  L%=wfsb"                           "\n\t"
      "cli"                                     "\n\t"

    // Ensure we can get exactly 7500 cycles (result = 1500)
      "nop"                                     "\n\t"
      "nop"                                     "\n\t"

    // Time the first segment (start bit + 3 least-significant bits of 'x'; lo bits)
    "L%=wfs1: "
      "adiw  %[reseult], 1"                     "\n\t"
      "sbis  %[calreg], %[calbit]"              "\n\t"
      "rjmp  L%=wfs1"                           "\n\t"

    // Time the second segment (middle bits of 'x'; hi bits)
    "L%=wfs2: "
      "adiw  %[reseult], 1"                     "\n\t"
      "sbic  %[calreg], %[calbit]"              "\n\t"
      "rjmp  L%=wfs2"                           "\n\t"

    // Finish at the third segment (most significant bit of 'x'; lo bit; hi stop bit terminates)
    "L%=wfs3: "
      "adiw  %[reseult], 1"                     "\n\t"
      "sbis  %[calreg], %[calbit]"              "\n\t"
      "rjmp  L%=wfs3"                           "\n\t"

      "sei"                                     "\n\t"

      : 
        [reseult] "+w" ( Temp )
      : 
        [calreg] "I" ( CAL_REG ),
        [calbit] "I" ( CAL_BIT )
    );
    
    // Put the clock back the way we found it
    cli();
    CLKPR = _BV(CLKPCE);
    CLKPR = ClockDivisor;
    sei();

    return( Temp );
  }

};


typedef TinyTuner<0x16,0x00> Tiny84Tuner;


#endif
