/*jshint browser: true */
/*jshint unused: false */
/*global require, exports, Backbone, EJS, window, SwaggerUi, hljs, document, $, arango */
/*global templateEngine*/

(function() {
  "use strict";

  window.AppDocumentationView = Backbone.View.extend({

    el: '#content',
    template: templateEngine.createTemplate("appDocumentationView.ejs"),

    initialize: function() {
      var internal = require("internal");
      var url = internal.arango.databasePrefix("/_admin/aardvark/docu/" + this.options.key);
      this.swaggerUi = new SwaggerUi({
          discoveryUrl: url,
          apiKey: false,
          dom_id: "swagger-ui-container",
          supportHeaderParams: true,
          supportedSubmitMethods: ['get', 'post', 'put', 'delete', 'patch', 'head'],
          onComplete: function(swaggerApi, swaggerUi){
            $('pre code').each(function(i, e) {hljs.highlightBlock(e);});
          },
          onFailure: function(data) {
            var div = document.createElement("div"),
              strong = document.createElement("strong");
            strong.appendChild(
              document.createTextNode("Sorry the code is not documented properly")
            );
            div.appendChild(strong);
            div.appendChild(document.createElement("br"));
            div.appendChild(document.createTextNode(JSON.stringify(data)));
            $("#swagger-ui-container").append(div);
          },
          docExpansion: "none"
      });
    },

    render: function(){
      $(this.el).html(this.template.render({}));
      this.swaggerUi.load();
      return this;
    }
  });
}());
