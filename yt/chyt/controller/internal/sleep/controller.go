package sleep

import (
	"context"
	"reflect"

	"a.yandex-team.ru/library/go/core/log"
	"a.yandex-team.ru/yt/chyt/controller/internal/strawberry"
	"a.yandex-team.ru/yt/go/ypath"
	"a.yandex-team.ru/yt/go/yson"
	"a.yandex-team.ru/yt/go/yt"
)

type Controller struct{}

func (c Controller) Prepare(ctx context.Context, oplet strawberry.Oplet) (
	spec map[string]interface{}, description map[string]interface{}, annotations map[string]interface{}, err error) {
	err = nil
	spec = map[string]interface{}{
		"tasks": map[string]interface{}{
			"main": map[string]interface{}{
				"command":   "sleep 10000",
				"job_count": 1,
			},
		}}
	description = map[string]interface{}{
		"sleeper_foo": "I am sleeper, look at me!",
	}
	annotations = map[string]interface{}{
		"sleeper_bar": "Actually I'd like to wake up :)",
	}
	return
}

func (c Controller) Family() string {
	return "sleep"
}

func (c Controller) NeedRestartOnSpecletChange(oldSpecletYson, newSpecletYson yson.RawValue) bool {
	var oldSpeclet, newSpeclet struct {
		TestOption *string `yson:"test_option"`
	}
	_ = yson.Unmarshal(oldSpecletYson, &oldSpeclet)
	_ = yson.Unmarshal(newSpecletYson, &newSpeclet)
	return !reflect.DeepEqual(oldSpeclet, newSpeclet)
}

func NewController(l log.Logger, ytc yt.Client, root ypath.Path, cluster string, config yson.RawValue) strawberry.Controller {
	return Controller{}
}
