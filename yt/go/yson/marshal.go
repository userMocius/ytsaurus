package yson

import (
	"bytes"
	"encoding"
	"fmt"
	"io"
	"reflect"
	"time"
)

// Encoder writes YSON to output stream.
type Encoder struct {
	w *Writer
}

func NewEncoder(w io.Writer) *Encoder {
	return &Encoder{NewWriter(w)}
}

func NewEncoderWriter(w *Writer) *Encoder {
	return &Encoder{w}
}

// Marshaler is an interface implemented by types that can encode themselves to YSON.
type Marshaler interface {
	MarshalYSON() ([]byte, error)
}

// StreamMarhsaler is an interface implemented by types that can encode themselves to YSON.
type StreamMarhsaler interface {
	MarshalYSON(*Writer) error
}

func (e *Encoder) Encode(value interface{}) (err error) {
	err = encodeAny(e.w, value)
	if err != nil {
		return
	}

	err = e.w.w.Flush()
	return
}

// Marshal returns YSON encoding of value.
//
// Marshal traverses value recursively.
//
// If value implements Marshaler interface, Marshal calls its MarshalYSON method to produce YSON.
//
// If value implements StreamMarhsaler interface, Marshal calls its MarshalYSON method to produce YSON.
//
// Otherwise, the following default encoding is used.
//
// Boolean values are encoded as YSON booleans.
//
// Floating point values are encoded as YSON float64.
//
// Unsigned integer types uint16, uint32, uint64 and uint are encoded as unsigned YSON integers.
//
// Signed integer types int16, int32, int64 and int are encoded as signed YSON integers.
//
// string and []byte values are encoded as YSON strings. Note that YSON string are always binary.
//
// Slice values are encoded as YSON lists (with exception of []byte).
//
// Struct values are encoded as YSON maps by default. Encoding of each struct field can be customized by format string
// stored under the "yson" key in the field's tag.
//
//     // Field appears in YSON under key "my_field".
//     Field int `yson:"my_field"`
//
//     // Field appears as attribute with name "my_attr".
//     Field int `yson:"my_attr,attr"`
//
//     // Field encoding completely replaces encoding of the whole struct.
//     // Other fields annotated as ",attr" are encoded as attributes preceding the value.
//     // All other fields are ignored.
//     Field int `yson:",value"`
//
//     // Field is skipped if empty.
//     Field int `yson:",omitempty"'
//
//     // Field is ignored by this package
//     Field int `yson:"-"`
//
//     // Field appears in YSON under key "-"
//     Field int `yson:"-,"`
//
// Map values are encoded as YSON maps. A map's key type must be a string.
//
// Pointer values are encoded as the value pointed to. A nil pointer encodes as the YSON entity value.
//
// Values implementing encoding.TextMarshaler and encoding.BinaryMarshaler interface are encoded as YSON strings.
//
// Interface values are encoded as the value contained in the interface. A nil interface value encodes as the YSON entity value.
func Marshal(value interface{}) ([]byte, error) {
	return MarshalFormat(value, FormatText)
}

func MarshalFormat(value interface{}, format Format) ([]byte, error) {
	var buf bytes.Buffer
	writer := NewWriterFormat(&buf, format)
	encoder := Encoder{writer}
	err := encoder.Encode(value)
	return buf.Bytes(), err
}

func encodeAny(w *Writer, value interface{}) (err error) {
	if value == nil {
		w.Entity()
		return w.Err()
	}

	switch vv := value.(type) {
	case bool:
		w.Bool(vv)
	case *bool:
		if vv != nil {
			w.Bool(*vv)
		} else {
			w.Entity()
		}

	case int16:
		w.Int64(int64(vv))
	case int32:
		w.Int64(int64(vv))
	case int64:
		w.Int64(vv)
	case int:
		w.Int64(int64(vv))

	case *int16:
		if vv != nil {
			w.Int64(int64(*vv))
		} else {
			w.Entity()
		}
	case *int32:
		if vv != nil {
			w.Int64(int64(*vv))
		} else {
			w.Entity()
		}
	case *int64:
		if vv != nil {
			w.Int64(*vv)
		} else {
			w.Entity()
		}
	case *int:
		if vv != nil {
			w.Int64(int64(*vv))
		} else {
			w.Entity()
		}

	case uint16:
		w.Uint64(uint64(vv))
	case uint32:
		w.Uint64(uint64(vv))
	case uint64:
		w.Uint64(vv)
	case uint:
		w.Uint64(uint64(vv))

	case *uint16:
		if vv != nil {
			w.Uint64(uint64(*vv))
		} else {
			w.Entity()
		}
	case *uint32:
		if vv != nil {
			w.Uint64(uint64(*vv))
		} else {
			w.Entity()
		}
	case *uint64:
		if vv != nil {
			w.Uint64(*vv)
		} else {
			w.Entity()
		}
	case *uint:
		if vv != nil {
			w.Uint64(uint64(*vv))
		} else {
			w.Entity()
		}

	case string:
		w.String(vv)
	case *string:
		if vv != nil {
			w.String(*vv)
		} else {
			w.Entity()
		}

	case []byte:
		w.Bytes(vv)
	case *[]byte:
		if vv != nil {
			w.Bytes(*vv)
		} else {
			w.Entity()
		}

	case float32:
		w.Float64(float64(vv))
	case float64:
		w.Float64(vv)

	case *float32:
		if vv != nil {
			w.Float64(float64(*vv))
		} else {
			w.Entity()
		}
	case *float64:
		if vv != nil {
			w.Float64(*vv)
		} else {
			w.Entity()
		}

	case RawValue:
		w.RawNode([]byte(vv))

	case ValueWithAttrs:
		if len(vv.Attrs) != 0 {
			w.BeginAttrs()

			for k, attr := range vv.Attrs {
				w.MapKeyString(k)

				if err = encodeAny(w, attr); err != nil {
					return err
				}
			}

			w.EndAttrs()
		}

		if err = encodeAny(w, vv.Value); err != nil {
			return err
		}

	case map[string]interface{}:
		w.BeginMap()
		for k, item := range vv {
			w.MapKeyString(k)

			if err = encodeAny(w, item); err != nil {
				return err
			}
		}
		w.EndMap()

	case Marshaler:
		var raw []byte
		raw, err = vv.MarshalYSON()
		if err != nil {
			return
		}
		w.RawNode(raw)

	case StreamMarhsaler:
		if err = vv.MarshalYSON(w); err != nil {
			return err
		}

	case Time:
		var ts string
		ts, err = MarshalTime(vv)
		if err != nil {
			return
		}
		w.String(ts)

	case *Time:
		if vv != nil {
			var ts string
			ts, err = MarshalTime(*vv)
			if err != nil {
				return
			}
			w.String(ts)
		} else {
			w.Entity()
		}

	case Duration:
		w.Int64(int64(time.Duration(vv) / time.Millisecond))

	case *Duration:
		if vv != nil {
			w.Int64(int64(time.Duration(*vv) / time.Millisecond))
		} else {
			w.Entity()
		}

	case encoding.TextMarshaler:
		var text []byte
		if text, err = vv.MarshalText(); err != nil {
			return err
		}
		w.Bytes(text)

	case encoding.BinaryMarshaler:
		var bin []byte
		if bin, err = vv.MarshalBinary(); err != nil {
			return err
		}
		w.Bytes(bin)

	default:
		err = encodeReflect(w, reflect.ValueOf(vv))
		if err != nil {
			return err
		}
	}

	return w.Err()
}

func encodeReflect(w *Writer, value reflect.Value) error {
	switch value.Type().Kind() {
	case reflect.Int, reflect.Int16, reflect.Int32, reflect.Int64:
		w.Int64(value.Int())
		return w.Err()

	case reflect.Uint, reflect.Uint16, reflect.Uint32, reflect.Uint64:
		w.Uint64(value.Uint())
		return w.Err()

	case reflect.String:
		w.String(value.String())
		return w.Err()

	case reflect.Struct:
		return encodeReflectStruct(w, value)
	case reflect.Slice:
		return encodeReflectSlice(w, value)
	case reflect.Ptr:
		if value.IsNil() {
			w.Entity()
			return w.Err()
		}

		return encodeAny(w, value.Elem().Interface())
	case reflect.Map:
		return encodeReflectMap(w, value)
	}

	return fmt.Errorf("yson: type %T not supported", value.Interface())
}

func encodeReflectMap(w *Writer, value reflect.Value) (err error) {
	if value.Type().Key().Kind() != reflect.String {
		return fmt.Errorf("yson: maps with non string keys are not supported")
	}

	w.BeginMap()

	mr := value.MapRange()
	for mr.Next() {
		w.MapKeyString(mr.Key().String())

		if err = encodeAny(w, mr.Value().Interface()); err != nil {
			return err
		}
	}

	w.EndMap()
	return w.Err()
}

func isEmptyValue(v reflect.Value) bool {
	switch v.Kind() {
	case reflect.Array, reflect.Map, reflect.Slice, reflect.String:
		return v.Len() == 0
	case reflect.Bool:
		return !v.Bool()
	case reflect.Int, reflect.Int8, reflect.Int16, reflect.Int32, reflect.Int64:
		return v.Int() == 0
	case reflect.Uint, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64, reflect.Uintptr:
		return v.Uint() == 0
	case reflect.Float32, reflect.Float64:
		return v.Float() == 0
	case reflect.Interface, reflect.Ptr:
		return v.IsNil()
	}
	return false
}

func encodeReflectStruct(w *Writer, value reflect.Value) (err error) {
	t := getStructType(value)

	encodeMapFragment := func(fields []*field) (err error) {
		for _, field := range fields {
			fieldValue, ok := fieldByIndex(value, field.index, false)
			if !ok {
				continue
			}

			if field.omitempty && isEmptyValue(fieldValue) {
				continue
			}

			w.MapKeyString(field.name)
			if err = encodeAny(w, fieldValue.Interface()); err != nil {
				return
			}
		}

		return
	}

	if t.attributes != nil {
		w.BeginAttrs()

		if err = encodeMapFragment(t.attributes); err != nil {
			return
		}

		w.EndAttrs()
	}

	if t.value != nil {
		return encodeAny(w, value.FieldByIndex(t.value.index).Interface())
	}

	w.BeginMap()

	if err = encodeMapFragment(t.fields); err != nil {
		return
	}

	w.EndMap()

	return w.Err()
}

func encodeReflectSlice(w *Writer, value reflect.Value) error {
	w.BeginList()
	for i := 0; i < value.Len(); i++ {
		if err := encodeAny(w, value.Index(i).Interface()); err != nil {
			return err
		}
	}
	w.EndList()

	return w.Err()
}
