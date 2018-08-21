pipeline {
    agent { label 'windows' }

    options { checkoutToSubdirectory('cse-core') }

    environment {
        CONAN_USER_HOME = "${env.BASE}\\conan-repositories\\${env.EXECUTOR_NUMBER}"
        CONAN_USER_HOME_SHORT = "${env.CONAN_USER_HOME}"
    }

    stages {
        stage('Conan add remote') {
            steps {
                bat 'conan remote add bincrafters https://api.bintray.com/conan/bincrafters/public-conan --force'
            }
        }
        stage('Build Debug') {
            steps {
                dir('debug-build') {
                    bat 'conan install ../cse-core -s build_type=Debug -b missing'
                    bat 'cmake -DCMAKE_INSTALL_PREFIX=../install -DCSECORE_USING_CONAN=TRUE -DCSECORE_BUILD_PRIVATE_APIDOC=ON -G "Visual Studio 15 2017 Win64" ../cse-core'
                    bat 'cmake --build . --config Debug'
                }
            }
        }
        stage('Build Release') {
            steps {
                dir('release-build') {
                    bat 'conan install ../cse-core -s build_type=Release -b missing'
                    bat 'cmake -DCMAKE_INSTALL_PREFIX=../install -DCSECORE_USING_CONAN=TRUE -DCSECORE_BUILD_PRIVATE_APIDOC=ON -G "Visual Studio 15 2017 Win64" ../cse-core'
                    bat 'cmake --build . --config Release'
                }
            }
        }
        stage ('Test Debug') {
            steps {
                dir('debug-build') {
                    bat 'ctest -C Debug -T Test'
                }
            }
            post {
                always{
                    xunit (
                        testTimeMargin: '30000',
                        thresholdMode: 1,
                        thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                        tools: [ CTest(pattern: 'debug-build/Testing/**/Test.xml') ]
                    )
                }
            }
        }
        stage ('Test Release') {
            steps {
                dir('release-build') {
                    bat 'ctest -C Release -T Test'
                }
            }
            post {
                always{
                    xunit (
                        testTimeMargin: '30000',
                        thresholdMode: 1,
                        thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                        tools: [ CTest(pattern: 'release-build/Testing/**/Test.xml') ]
                    )
                }
            }
        }            
    }
}
