module.exports = function (grunt) {

    grunt.loadNpmTasks("grunt-contrib-jshint");


    grunt.initConfig({

        jshint:{
            options:{
                jshintrc: ".jshintrc"
            },
            files : ["./tasks/**/*.js"]
        }
    });


    grunt.registerTask("default", ["jshint"]);
};