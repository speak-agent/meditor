// {{name}} — created by meditor (GUI template)
module app;
import huxerui;

const huxerui::Application application{
    [] {
        return huxerui::Column {
            huxerui::Text("Hello from {{name}}!"),
        }.With(huxerui::Padding(32.0F));
    },
    {
        .window = {
            .title = "{{name}}",
            .initial_size = {520.0F, 360.0F},
        },
    },
};
